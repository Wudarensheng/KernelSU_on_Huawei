#ifndef __BSP_TRANS_REPORT_H__
#define __BSP_TRANS_REPORT_H__

#include "product_config.h"

#define TRANS_IF_NAME_SIZE 16
#define TRANS_PKT_HDR_LEN 60

/* get pkt info in each point */
enum trans_report_type {
    trans_type_spe_dl = 0,
    trans_type_spe_ul = 1,
    trans_type_ipf_dl = 2,
    trans_type_ipf_ul = 3,
    trans_type_usb_tx = 4,
    trans_type_usb_tx_complete = 5,
    trans_type_usb_rx = 6,
    trans_type_weth_rx = 7,
    trans_type_weth_tx = 8,
    trans_type_max
};

enum trans_report_msg_type {
    /* msgid for trans debug info for hids */
    TRANS_MSGID_MAA = 0x9f383331,
    TRANS_MSGID_SPE,
    TRANS_MSGID_WAN,
    TRANS_MSGID_WETH,
    TRANS_MSGID_PCDEV,
    TRANS_MSGID_GMAC,
    TRANS_MSGID_USB,
    TRANS_MSGID_WETH_RC,
    /* msgid for trans pkt info */
    TRANS_PKT_MSGID_BASE = 0x9f383340,
    TRANS_PKT_MSGID_MAX = 0x9f383350,
};

struct trans_report_pkt_info {
    unsigned short l3_version;
    unsigned short l4_protocol;
    unsigned int l3_id;
    unsigned int l4_seq;
    unsigned int l4_ack;
    unsigned int time_stamp;
};

struct weth_hids_trans_open_msg {
    unsigned short en_type;
    unsigned short enable;
};

struct trans_msg_info {
    unsigned short prim_id;
    unsigned short tool_id;

    unsigned char net_if_name[TRANS_IF_NAME_SIZE];
    unsigned short len;
    unsigned short data[2];
};

struct trans_report_default_pkt {
    struct trans_msg_info msg_hdr;
    unsigned char data[TRANS_PKT_HDR_LEN];
};

enum TRANS_HOOK_ID {
    TRANS_HOOK_BR_PRE_ROUTING = 0x00,
    TRANS_HOOK_BR_POST_ROUTING = 0x01,
    TRANS_HOOK_BR_FORWARD = 0x02,
    TRANS_HOOK_BR_LOCAL_IN = 0x03,
    TRANS_HOOK_BR_LOCAL_OUT = 0x04,
    TRANS_HOOK_ARP_LOCAL_IN = 0x05,
    TRANS_HOOK_ARP_LOCAL_OUT = 0x06,
    TRANS_HOOK_IP4_PRE_ROUTING = 0x07,
    TRANS_HOOK_IP4_POST_ROUTING = 0x08,
    TRANS_HOOK_IP4_LOCAL_IN = 0x09,
    TRANS_HOOK_IP4_LOCAL_OUT = 0x0A,
    TRANS_HOOK_IP4_FORWARD = 0x0B,
    TRANS_HOOK_IP6_PRE_ROUTING = 0x0C,
    TRANS_HOOK_IP6_POST_ROUTING = 0x0D,
    TRANS_HOOK_IP6_LOCAL_IN = 0x0E,
    TRANS_HOOK_IP6_LOCAL_OUT = 0x0F,
    TRANS_HOOK_IP6_FORWARD = 0x10,
    TRANS_HOOK_BR_FORWARD_FLOW_CTRL = 0x11,
    TRANS_HOOK_ENUM_BUTT
};

struct trans_report_ctx {
    unsigned long long last_jiff;
    unsigned long long last_slice;

    unsigned int stat_addr_err;
    unsigned int stat_addr_set_err;
    unsigned int stat_addr_protocol_err;
    unsigned int stat_addr_pkt_other;
    unsigned int stat_addr_pkt_succ;
    unsigned int stat_addr_pkt_;
    unsigned int stat_addr_trans_fail;
    unsigned int stat_alloc_fail;
    unsigned int stat_buff_err;
    unsigned int stat_buff_full;
    unsigned int stat_buff_ok;
    unsigned int stat_addr_trans_succ;
    unsigned int stat_addr_trans_byte;
    unsigned int stat_send_process;
    unsigned int stat_last_err;
    unsigned int stat_last_length;
    unsigned int stat_last_num;
    unsigned int stat_not_enable;

    unsigned int stat_rc_default_pkt;
    unsigned int stat_rc_all_pkt;
    unsigned int stat_rc_pkt_succ;
    unsigned int stat_rc_pkt_fail;
};

#ifdef CONFIG_BALONG_TRANS_REPORT /* ep */
void bsp_trans_report_mark_pkt_info(unsigned long long maa_addr, enum trans_report_type trans_type, bool is_skb,
                                    bool is_cache);
void bsp_trans_report_set_time(void);
void bsp_trans_report_enable_by_type(enum trans_report_type trans_type, int enable);
void bsp_trans_report_rc_pkt(unsigned int msg_id, void *data, unsigned int len);

#else
static inline void bsp_trans_report_mark_pkt_info(unsigned long long maa_addr, enum trans_report_type trans_type,
                                                  bool is_skb, bool is_cache)
{
    return;
}
static inline void bsp_trans_report_set_time(void)
{
    return;
}
static inline void bsp_trans_report_enable_by_type(enum trans_report_type trans_type, int enable)
{
    return;
}

static inline void bsp_trans_report_rc_pkt(unsigned int msg_id, void *data, unsigned int len)
{
    return;
}

#endif

#ifdef CONFIG_BALONG_WETH
void bsp_trans_report_enable_all_pkt(unsigned int enable);
void bsp_trans_enable_hooks(void);

#else
static inline void bsp_trans_report_enable_all_pkt(unsigned int enable)
{
    return;
}
static inline void bsp_trans_enable_hooks(void)
{
    return;
}

#endif

#endif
