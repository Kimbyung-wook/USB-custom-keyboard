#include "CH58x_common.h"
#include "CH58xBLE_LIB.h"
#include "ble_host.h"
#include "store.h"
#include "core/hogp.h"
#include "core/link.h"
#include <string.h>

// ---- tuning -----------------------------------------------------------------
#define SCAN_MS 1000          // one pairing scan round
#define PAIR_INIT_MS 3000     // connect attempt to a scanned device
#define SETUP_MS 20000        // link up -> ready, else drop
#define RETRY_MS 20           // GATT request refused (buffers busy)
#define MAX_CAND 8
#define MAX_REJECT 4

// TMOS events
#define EV_START 0x0001
#define EV_RETRY 0x0002
#define EV_SETUP_TO 0x0004
#define EV_INIT_TO 0x0008
#define EV_LED 0x0010

enum { S_IDLE, S_SCAN, S_INIT, S_CONN, S_CANCEL };
enum { P_NONE, P_WAIT_SEC, P_SVC, P_CHARS, P_DESCS, P_REFS, P_MAP, P_CCCD, P_READY };

typedef struct {
    uint8_t type, addr[6];
    int8_t rssi;
    uint8_t adv, conn; // adv_parse bits, connectable
} cand_t;

static uint8_t task;
static uint8_t started, st, want, init_target;
static uint16_t conn = GAP_CONNHANDLE_INIT;
static uint8_t phase, idx, secured, cached, leds, led_dirty;

static store_dev_t bond, cur;   // bonded device; device on the link
static uint8_t bond_ok;
static hd_map_t map;
static conv_t conv;
static conv_emit_t emit_cb;
static bh_event_t event_cb;

static cand_t cand[MAX_CAND];
static uint8_t ncand;
static uint8_t reject[MAX_REJECT][7];
static uint8_t nreject;

static void kick(void);
static void map_done(void);

// ---- helpers ----------------------------------------------------------------
static uint8_t same_addr(uint8_t ta, const uint8_t *a, uint8_t tb, const uint8_t *b) {
    return ta == tb && !memcmp(a, b, 6);
}

static void erase_single(uint8_t type, const uint8_t *addr) {
    uint8_t p[7];
    p[0] = type;
    memcpy(p + 1, addr, 6);
    GAPBondMgr_SetParameter(GAPBOND_ERASE_SINGLEBOND, sizeof(p), p);
}

static uint8_t rejected(uint8_t type, const uint8_t *addr) {
    for (uint8_t i = 0; i < nreject; i++)
        if (same_addr(reject[i][0], reject[i] + 1, type, addr)) return 1;
    return 0;
}

static void add_reject(uint8_t type, const uint8_t *addr) {
    if (rejected(type, addr)) return;
    uint8_t i = nreject < MAX_REJECT ? nreject++ : MAX_REJECT - 1;
    reject[i][0] = type;
    memcpy(reject[i] + 1, addr, 6);
}

static uint8_t dropping; // terminate asked, waiting for the link-down event

static void drop_link(void) {
    if (conn != GAP_CONNHANDLE_INIT && !dropping && GAPRole_TerminateLink(conn) == SUCCESS) dropping = 1;
}

// ---- GATT requests ----------------------------------------------------------
static bStatus_t write_req(uint16_t handle, const uint8_t *v, uint8_t len, uint8_t cmd) {
    attWriteReq_t req;
    req.handle = handle;
    req.len = len;
    req.sig = 0;
    req.cmd = cmd;
    req.pValue = GATT_bm_alloc(conn, cmd ? ATT_WRITE_CMD : ATT_WRITE_REQ, len, NULL, 0);
    if (!req.pValue) return bleMemAllocError;
    memcpy(req.pValue, v, len);
    bStatus_t s = cmd ? GATT_WriteNoRsp(conn, &req) : GATT_WriteCharValue(conn, &req, task);
    if (s != SUCCESS) GATT_bm_free((gattMsg_t *)&req, cmd ? ATT_WRITE_CMD : ATT_WRITE_REQ);
    return s;
}

static uint16_t cccd_handle(uint8_t i) {
    if (cur.boot) return i == 0 ? cur.hogp.boot_in_cccd : 0;
    if (i >= cur.hogp.nrep) return 0;
    const hogp_report_t *r = &cur.hogp.rep[i];
    return r->type == HOGP_REP_INPUT ? r->cccd : 0;
}

// Issue the request of the current phase/index; retried later if refused
static void kick(void) {
    bStatus_t s = SUCCESS;
    hogp_t *h = &cur.hogp;
    switch (phase) {
    case P_SVC: {
        uint8_t uuid[2] = {UUID_HID_SERVICE & 0xFF, UUID_HID_SERVICE >> 8};
        s = GATT_DiscPrimaryServiceByUUID(conn, uuid, 2, task);
        break;
    }
    case P_CHARS: s = GATT_DiscAllChars(conn, h->start, h->end, task); break;
    case P_DESCS: s = GATT_DiscAllCharDescs(conn, h->start, h->end, task); break;
    case P_REFS: {
        attReadReq_t r = {.handle = h->rep[idx].ref};
        s = GATT_ReadCharValue(conn, &r, task);
        break;
    }
    case P_MAP: {
        attReadBlobReq_t r = {.handle = h->map, .offset = 0};
        cur.map_len = 0;
        s = GATT_ReadLongCharValue(conn, &r, task);
        break;
    }
    case P_CCCD: {
        uint8_t on[2] = {0x01, 0x00};
        s = write_req(cccd_handle(idx), on, 2, 0);
        break;
    }
    default: return;
    }
    if (s != SUCCESS) tmos_start_task(task, EV_RETRY, MS1_TO_SYSTEM_TIME(RETRY_MS));
}

static void send_leds(void) {
    if (phase != P_READY) return;
    uint8_t out[8], id = 0, len;
    uint16_t h = 0;
    if (cur.boot) {
        h = cur.hogp.boot_out;
        out[0] = leds & 0x1F;
        len = 1;
    } else {
        len = conv_led_report(&map, leds, &id, out, sizeof(out));
        int8_t r = len ? hogp_find_output(&cur.hogp, id) : -1;
        if (r >= 0) h = cur.hogp.rep[r].val;
    }
    led_dirty = 0;
    if (h && len && write_req(h, out, len, 1) != SUCCESS) {
        led_dirty = 1;
        tmos_start_task(task, EV_LED, MS1_TO_SYSTEM_TIME(RETRY_MS));
    }
}

// ---- discovery steps ----------------------------------------------------------
static void next_ref(void) {
    while (idx < cur.hogp.nrep && !cur.hogp.rep[idx].ref) idx++;
    if (idx < cur.hogp.nrep) {
        phase = P_REFS;
        kick();
    } else if (cur.hogp.map) {
        phase = P_MAP;
        kick();
    } else {
        cur.map_len = 0;
        phase = P_MAP; // handled as "no map" below
        map_done();
    }
}

static void next_cccd(void) {
    uint8_t n = cur.boot ? 1 : cur.hogp.nrep;
    while (idx < n && !cccd_handle(idx)) idx++;
    if (idx < n) {
        phase = P_CCCD;
        kick();
        return;
    }
    // ready
    tmos_stop_task(task, EV_SETUP_TO);
    phase = P_READY;
    if (!cached || init_target == LK_TARGET_ANY) {
        if (bond_ok && !same_addr(bond.addr_type, bond.addr, cur.addr_type, cur.addr))
            erase_single(bond.addr_type, bond.addr);
        memcpy(&bond, &cur, sizeof(bond));
        store_save(&bond);
        bond_ok = 1;
    }
    init_target = LK_TARGET_BOND; // this device is the bond now
    nreject = 0;
    event_cb(LKE_READY);
    send_leds();
}

static uint8_t load_map(void) {
    if (cur.boot) return hd_parse(&map, hd_boot_kb_desc, hd_boot_kb_desc_len) == 0;
    hd_parse(&map, cur.map, cur.map_len); // keep what parsed even if malformed
    if (!map.n) return 0;
    for (uint8_t i = 0; i < cur.hogp.nrep; i++)
        if (cccd_handle(i)) return 1;
    return 0;
}

static void start_cccd(void) {
    conv_init(&conv, &map, emit_cb, 0);
    if (cur.hogp.proto) {
        uint8_t mode = cur.boot ? 0 : 1; // Protocol Mode: 0 boot, 1 report
        write_req(cur.hogp.proto, &mode, 1, 1);
    }
    idx = 0;
    next_cccd();
}

static void map_done(void) {
    cur.boot = 0;
    if (!load_map()) {
        cur.boot = 1;
        cur.map_len = 0;
        if (!cur.hogp.boot_in_cccd || !load_map()) {
            add_reject(cur.addr_type, cur.addr); // no usable HID input
            drop_link();
            return;
        }
    }
    start_cccd();
}

static void start_discovery(void) {
    cached = 0;
    memset(&cur.hogp, 0, sizeof(cur.hogp));
    cur.map_len = 0;
    phase = P_SVC;
    kick();
}

static void on_secured(void) {
    if (secured) return;
    secured = 1;
    if (cached && load_map()) start_cccd();
    else start_discovery();
}

// ---- GATT messages ----------------------------------------------------------
static void on_notify(uint16_t handle, const uint8_t *v, uint16_t len) {
    if (phase < P_CCCD) return;
    if (cur.boot) {
        if (handle == cur.hogp.boot_in) conv_input(&conv, 0, v, len);
        return;
    }
    int8_t r = hogp_find(&cur.hogp, handle);
    if (r < 0 || cur.hogp.rep[r].type != HOGP_REP_INPUT) return;
    conv_input(&conv, map.uses_ids ? cur.hogp.rep[r].id : 0, v, len);
}

static void gatt_msg(gattMsgEvent_t *m) {
    if (m->connHandle != conn) return;
    uint8_t done = m->hdr.status == bleProcedureComplete;
    hogp_t *h = &cur.hogp;
    switch (m->method) {
    case ATT_HANDLE_VALUE_NOTI:
        on_notify(m->msg.handleValueNoti.handle, m->msg.handleValueNoti.pValue, m->msg.handleValueNoti.len);
        return;
    case ATT_HANDLE_VALUE_IND:
        on_notify(m->msg.handleValueInd.handle, m->msg.handleValueInd.pValue, m->msg.handleValueInd.len);
        ATT_HandleValueCfm(conn);
        return;
    case ATT_ERROR_RSP:
        switch (phase) {
        case P_SVC: case P_CHARS: case P_DESCS: done = 1; break; // "not found" ends the step
        case P_REFS: idx++; next_ref(); return;
        case P_MAP: map_done(); return;                        // keep a partial map
        case P_CCCD:
            if (cached) { start_discovery(); return; }            // handles changed
            idx++; next_cccd(); return;
        default: return;
        }
        break;
    default: break;
    }

    switch (phase) {
    case P_SVC:
        if (m->method == ATT_FIND_BY_TYPE_VALUE_RSP && m->msg.findByTypeValueRsp.numInfo && !h->start) {
            h->start = ATT_ATTR_HANDLE(m->msg.findByTypeValueRsp.pHandlesInfo, 0);
            h->end = ATT_GRP_END_HANDLE(m->msg.findByTypeValueRsp.pHandlesInfo, 0);
        }
        if (done) {
            if (!h->start) {
                add_reject(cur.addr_type, cur.addr);
                drop_link();
                return;
            }
            hogp_reset(h, h->start, h->end);
            phase = P_CHARS;
            kick();
        }
        break;
    case P_CHARS:
        if (m->method == ATT_READ_BY_TYPE_RSP && m->msg.readByTypeRsp.len == 7)
            for (uint16_t i = 0; i < m->msg.readByTypeRsp.numPairs; i++) {
                const uint8_t *p = m->msg.readByTypeRsp.pDataList + i * 7;
                hogp_add_char(h, BUILD_UINT16(p[3], p[4]), BUILD_UINT16(p[5], p[6]));
            }
        if (done) {
            phase = P_DESCS;
            kick();
        }
        break;
    case P_DESCS:
        if (m->method == ATT_FIND_INFO_RSP && m->msg.findInfoRsp.format == 0x01)
            for (uint16_t i = 0; i < m->msg.findInfoRsp.numInfo; i++) {
                const uint8_t *p = m->msg.findInfoRsp.pInfo + i * 4;
                hogp_add_desc(h, BUILD_UINT16(p[0], p[1]), BUILD_UINT16(p[2], p[3]));
            }
        if (done) {
            idx = 0;
            next_ref();
        }
        break;
    case P_REFS:
        if (m->method == ATT_READ_RSP) {
            if (m->msg.readRsp.len >= 2) hogp_set_ref(h, idx, m->msg.readRsp.pValue[0], m->msg.readRsp.pValue[1]);
            idx++;
            next_ref();
        }
        break;
    case P_MAP:
        if (m->method == ATT_READ_BLOB_RSP || m->method == ATT_READ_RSP) {
            uint16_t n = m->msg.readBlobRsp.len;
            if (n > STORE_MAP_MAX - cur.map_len) n = STORE_MAP_MAX - cur.map_len;
            memcpy(cur.map + cur.map_len, m->msg.readBlobRsp.pValue, n);
            cur.map_len += n;
        }
        if (done) map_done();
        break;
    case P_CCCD:
        if (m->method == ATT_WRITE_RSP) {
            idx++;
            next_cccd();
        }
        break;
    default: break;
    }
}

// ---- GAP --------------------------------------------------------------------
static void start_scan(void) {
    if (want != init_target) nreject = 0; // new pairing session
    ncand = 0;
    init_target = LK_TARGET_ANY;
    if (GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE) == SUCCESS) st = S_SCAN;
}

static void connect_to(uint8_t type, const uint8_t *addr, uint8_t target) {
    init_target = target;
    if (GAPRole_CentralEstablishLink(TRUE, FALSE, type, (uint8_t *)addr) != SUCCESS) return;
    st = S_INIT;
    if (target == LK_TARGET_ANY) tmos_start_task(task, EV_INIT_TO, MS1_TO_SYSTEM_TIME(PAIR_INIT_MS));
}

static void on_adv(const gapDeviceInfoEvent_t *e) {
    uint8_t i;
    for (i = 0; i < ncand; i++)
        if (same_addr(cand[i].type, cand[i].addr, e->addrType, e->addr)) break;
    if (i == ncand) {
        if (ncand < MAX_CAND) ncand++;
        else { // replace the weakest
            i = 0;
            for (uint8_t k = 1; k < MAX_CAND; k++)
                if (cand[k].rssi < cand[i].rssi) i = k;
            if (e->rssi <= cand[i].rssi) return;
        }
        memset(&cand[i], 0, sizeof(cand[i]));
        cand[i].type = e->addrType;
        memcpy(cand[i].addr, e->addr, 6);
        cand[i].rssi = e->rssi;
    }
    if (e->rssi > cand[i].rssi) cand[i].rssi = e->rssi;
    cand[i].adv |= adv_parse(e->pEvtData, e->dataLen);
    if (e->eventType == GAP_ADRPT_ADV_IND || e->eventType == GAP_ADRPT_ADV_DIRECT_IND) cand[i].conn = 1;
}

static void on_scan_done(void) {
    st = S_IDLE;
    if (want != LK_TARGET_ANY) return;
    int8_t best = -1;
    for (uint8_t i = 0; i < ncand; i++) {
        const cand_t *c = &cand[i];
        if (c->conn && (c->adv & (ADV_HID | ADV_DISCOVERABLE)) == (ADV_HID | ADV_DISCOVERABLE) &&
            !rejected(c->type, c->addr) && (best < 0 || c->rssi > cand[best].rssi))
            best = i;
    }
    if (best >= 0) connect_to(cand[best].type, cand[best].addr, LK_TARGET_ANY);
}

static void on_link_up(const gapEstLinkReqEvent_t *e) {
    tmos_stop_task(task, EV_INIT_TO);
    if (e->hdr.status != SUCCESS) { // failed or cancelled
        st = S_IDLE;
        return;
    }
    st = S_CONN;
    conn = e->connectionHandle;
    dropping = 0;
    secured = 0;
    phase = P_WAIT_SEC;
    cached = bond_ok && same_addr(bond.addr_type, bond.addr, e->devAddrType, e->devAddr);
    if (cached) memcpy(&cur, &bond, sizeof(cur));
    else {
        memset(&cur, 0, sizeof(cur));
        cur.addr_type = e->devAddrType;
        memcpy(cur.addr, e->devAddr, 6);
    }
    tmos_start_task(task, EV_SETUP_TO, MS1_TO_SYSTEM_TIME(SETUP_MS));
    event_cb(LKE_LINK_UP);
}

static void on_link_down(void) {
    tmos_stop_task(task, EV_SETUP_TO);
    tmos_stop_task(task, EV_RETRY);
    tmos_stop_task(task, EV_LED);
    conn = GAP_CONNHANDLE_INIT;
    dropping = 0;
    st = S_IDLE;
    phase = P_NONE;
    conv_release_all(&conv);
    event_cb(LKE_LINK_DOWN);
    bh_poll(want); // reconnect at once
}

static void gap_event(gapRoleEvent_t *e) {
    switch (e->gap.opcode) {
    case GAP_DEVICE_INIT_DONE_EVENT: started = 1; break;
    case GAP_DEVICE_INFO_EVENT: on_adv(&e->deviceInfo); break;
    case GAP_DEVICE_DISCOVERY_EVENT: on_scan_done(); break;
    case GAP_LINK_ESTABLISHED_EVENT: on_link_up(&e->linkCmpl); break;
    case GAP_LINK_TERMINATED_EVENT:
        if (e->linkTerminate.connectionHandle == conn) on_link_down();
        break;
    default: break;
    }
}

static void pair_state(uint16_t handle, uint8_t state, uint8_t status) {
    if (handle != conn) return;
    if (state != GAPBOND_PAIRING_STATE_COMPLETE && state != GAPBOND_PAIRING_STATE_BONDED) return;
    if (status == SUCCESS) {
        on_secured();
        return;
    }
    // pairing / encryption failed
    if (init_target == LK_TARGET_ANY) {
        erase_single(cur.addr_type, cur.addr); // stale keys: pair fresh next time
        add_reject(cur.addr_type, cur.addr);
        if (bond_ok && same_addr(bond.addr_type, bond.addr, cur.addr_type, cur.addr)) {
            bond_ok = 0; // the keyboard lost the old bond and is being re-paired
            store_clear();
        }
    }
    drop_link();
}

static void passcode(uint8_t *addr, uint16_t handle, uint8_t in, uint8_t out) {
    GAPBondMgr_PasscodeRsp(handle, SUCCESS, 0); // shown as 000000: type it on the keyboard if asked
}

static void rssi_cb(uint16_t h, int8_t r) {}
static void len_cb(uint16_t h, uint16_t tx, uint16_t rx) {}

static gapCentralRoleCB_t role_cb = {rssi_cb, gap_event, len_cb};
static gapBondCBs_t bond_cb = {passcode, pair_state, NULL};

static tmosEvents bh_task(tmosTaskID id, tmosEvents ev) {
    if (ev & SYS_EVENT_MSG) {
        uint8_t *m = tmos_msg_receive(task);
        if (m) {
            if (((tmos_event_hdr_t *)m)->event == GATT_MSG_EVENT) {
                gattMsgEvent_t *g = (gattMsgEvent_t *)m;
                gatt_msg(g);
                GATT_bm_free(&g->msg, g->method);
            }
            tmos_msg_deallocate(m);
        }
        return ev ^ SYS_EVENT_MSG;
    }
    if (ev & EV_START) {
        GAPRole_CentralStartDevice(task, &bond_cb, &role_cb);
        return ev ^ EV_START;
    }
    if (ev & EV_RETRY) {
        kick();
        return ev ^ EV_RETRY;
    }
    if (ev & EV_SETUP_TO) {
        if (phase != P_READY) {
            if (init_target == LK_TARGET_ANY) add_reject(cur.addr_type, cur.addr);
            drop_link();
        }
        return ev ^ EV_SETUP_TO;
    }
    if (ev & EV_INIT_TO) {
        if (st == S_INIT) {
            GAPRole_TerminateLink(INVALID_CONNHANDLE); // cancel connecting
            st = S_CANCEL;
        }
        return ev ^ EV_INIT_TO;
    }
    if (ev & EV_LED) {
        if (led_dirty) send_leds();
        return ev ^ EV_LED;
    }
    return 0;
}

// ---- public -------------------------------------------------------------------
void bh_init(conv_emit_t emit, bh_event_t event) {
    emit_cb = emit;
    event_cb = event;
    bond_ok = store_load(&bond);
    conv_init(&conv, &map, emit_cb, 0);
    task = TMOS_ProcessEventRegister(bh_task);

    GAP_SetParamValue(TGAP_DISC_SCAN, MS1_TO_SYSTEM_TIME(SCAN_MS));
    GAP_SetParamValue(TGAP_DISC_SCAN_INT, 16); // 10 ms window every 10 ms
    GAP_SetParamValue(TGAP_DISC_SCAN_WIND, 16);
    GAP_SetParamValue(TGAP_CONN_EST_SCAN_INT, 16);
    GAP_SetParamValue(TGAP_CONN_EST_SCAN_WIND, 16);
    GAP_SetParamValue(TGAP_CONN_EST_HIGH_SCAN_INT, 16);
    GAP_SetParamValue(TGAP_CONN_EST_HIGH_SCAN_WIND, 16);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MIN, 6);  // 7.5 ms
    GAP_SetParamValue(TGAP_CONN_EST_INT_MAX, 12); // 15 ms
    GAP_SetParamValue(TGAP_CONN_EST_LATENCY, 0);
    GAP_SetParamValue(TGAP_CONN_EST_SUPERV_TIMEOUT, 200); // 2 s
    {
        uint32_t pass = 0;
        uint8_t mode = GAPBOND_PAIRING_MODE_INITIATE, mitm = FALSE, io = GAPBOND_IO_CAP_DISPLAY_ONLY, bonding = TRUE;
        GAPBondMgr_SetParameter(GAPBOND_CENT_DEFAULT_PASSCODE, sizeof(pass), &pass);
        GAPBondMgr_SetParameter(GAPBOND_CENT_PAIRING_MODE, 1, &mode);
        GAPBondMgr_SetParameter(GAPBOND_CENT_MITM_PROTECTION, 1, &mitm);
        GAPBondMgr_SetParameter(GAPBOND_CENT_IO_CAPABILITIES, 1, &io);
        GAPBondMgr_SetParameter(GAPBOND_CENT_BONDING_ENABLED, 1, &bonding);
    }
    GATT_InitClient();
    GATT_RegisterForInd(task);
    tmos_set_event(task, EV_START);
}

uint8_t bh_has_bond(void) { return bond_ok; }

void bh_poll(uint8_t target) {
    want = target;
    if (!started) return;
    switch (st) {
    case S_IDLE:
        if (want == LK_TARGET_ANY) start_scan();
        else if (want == LK_TARGET_BOND && bond_ok) connect_to(bond.addr_type, bond.addr, LK_TARGET_BOND);
        break;
    case S_SCAN:
        if (want != LK_TARGET_ANY && GAPRole_CentralCancelDiscovery() == SUCCESS) st = S_CANCEL;
        break;
    case S_INIT:
        if (want != init_target) {
            GAPRole_TerminateLink(INVALID_CONNHANDLE);
            st = S_CANCEL;
        }
        break;
    case S_CONN:
        if (want != init_target) drop_link(); // pairing asked while on the bond, or pairing ended
        break;
    default: break; // S_CANCEL: wait for the stack's event
    }
}

void bh_set_leds(uint8_t v) {
    leds = v;
    send_leds();
}

void bh_erase_all(void) {
    drop_link();
    GAPBondMgr_SetParameter(GAPBOND_ERASE_ALLBONDS, 0, NULL);
    store_clear();
    bond_ok = 0;
}
