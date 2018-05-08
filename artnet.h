#ifndef __ARTNET_H__
#define __ARTNET_H__

#include <hal.h>
#include <ustack.h>
#include <ustack_thread.h>

// How many groups of 4 ports we have ?

#define ARTNET_GROUPS 1
#define VERSION 0x01
#define OEM 0x0000
#define ESTA 0xffff

// No user serviceable parts below

#define ARTNET_VERSION 14
#define ARTNET_MAX_PORTS 4
#define ARTNET_SHORT_NAME_LENGTH 18
#define ARTNET_LONG_NAME_LENGTH 64
#define ARTNET_REPORT_LENGTH 64
#define ARTNET_DMX_LENGTH 512
#define ARTNET_PORT 6454
#define SACN_PORT 5568

#define ARTNET_OEM 0x04b6

// Defines

#define ARTNET_IPPROG_ENABLE    (1<<7)
#define ARTNET_IPPROG_DCHP      (1<<6)
#define ARTNET_IPPROG_DEF       (1<<3)
#define ARTNET_IPPROG_IP        (1<<2)
#define ARTNET_IPPROG_SUB       (1<<1)
#define ARTNET_IPPROG_PORT      (1<<0)

#define ARTNET_STATUS_INDICATOR_UNKNOWN ((0x00 << 6) & 0xc0)
#define ARTNET_STATUS_INDICATOR_LOCATE ((0x01 << 6) & 0xc0)
#define ARTNET_STATUS_INDICATOR_MUTE ((0x02 << 6) & 0xc0)
#define ARTNET_STATUS_INDICATOR_NORMAL ((0x03 << 6) & 0xc0)
#define ARTNET_STATUS_PROG_UNKNOWN ((0x00 << 4) & 0x30)
#define ARTNET_STATUS_PROG_PANEL ((0x01 << 4) & 0x30)
#define ARTNET_STATUS_PROG_NETWORK ((0x02 << 4) &0x30)
#define ARTNET_STATUS_RDM_ENABLED (1 << 1)
#define ARTNET_STATUS_RDM_DISABLED (0 << 1)

typedef enum
{
  ARTNET_TYPE_DMX512        = 0x00,
  ARTNET_TYPE_MIDI          = 0x01,
  ARTNET_TYPE_AVAB          = 0x02,
  ARTNET_TYPE_COLORTRAN_CMX = 0x03,
  ARTNET_TYPE_ADB625        = 0x04,
  ARTNET_TYPE_ARTNET        = 0x05,
  ARTNET_TYPE_INPUT         = 0x40,
  ARTNET_TYPE_OUTPUT        = 0x80
} artnet_porttypes_en;

typedef enum
{
  ARTNET_INPUT_ERRORS = 0x04,
  ARTNET_INPUT_DISABLED = 0x08,
  ARTNET_INPUT_TEXT = 0x10,
  ARTNET_INPUT_SIP = 0x20,
  ARTNET_INPUT_TEST = 0x40,
  ARTNET_INPUT_RECEIVED = 0x80
} artnet_input_status_en;

typedef enum
{
  ARTNET_OUTPUT_SACN = 0x01,
  ARTNET_OUTPUT_LTP = 0x02,
  ARTNET_OUTPUT_SHORT = 0x04,
  ARTNET_OUTPUT_MERGING = 0x08,
  ARTNET_OUTPUT_TEXT = 0x10,
  ARTNET_OUTPUT_SIP = 0x20,
  ARTNET_OUTPUT_TEST = 0x40,
  ARTNET_OUTPUT_TRANSMIT = 0x80
} artnet_output_status_en;

typedef enum
{
  ARTNET_STATUS2_HAS_WEB = 0x01,
  ARTNET_STATUS2_DHCP_CFG = 0x02,
  ARTNET_STATUS2_DHCP_EN = 0x04,
  ARTNET_STATUS2_ARTNETV4 = 0x08,
  ARTNET_STATUS2_HAS_SACN = 0x10,
  ARTNET_STATUS2_SQUAWK = 0x20
} artnet_status2_en;

typedef enum
{
  ARTNET_SRV,     /**< An ArtNet server (transmitts DMX data) */
  ARTNET_NODE,    /**< An ArtNet node   (dmx reciever) */
  ARTNET_MSRV,    /**< A Media Server */
  ARTNET_ROUTE,   /**< No Effect currently */
  ARTNET_BACKUP,  /**< No Effect currently */
  ARTNET_RAW      /**< Raw Node - used for diagnostics */
} artnet_node_type_en;

// the status of the node
typedef enum
{
  ARTNET_OFF,
  ARTNET_STANDBY,
  ARTNET_ON
} node_status_en;

typedef enum
{
  ARTNET_RCDEBUG,
  ARTNET_RCPOWEROK,
  ARTNET_RCPOWERFAIL,
  ARTNET_RCSOCKETWR1,
  ARTNET_RCPARSEFAIL,
  ARTNET_RCUDPFAIL,
  ARTNET_RCSHNAMEOK,
  ARTNET_RCLONAMEOK,
  ARTNET_RCDMXERROR,
  ARTNET_RCDMXUDPFULL,
  ARTNET_RCDMXRXFULL,
  ARTNET_RCSWITCHERR,
  ARTNET_RCCONFIGERR,
  ARTNET_RCDMXSHORT,
  ARTNET_RCFIRMWAREFAIL,
  ARTNET_RCUSERFAIL,
  ARTNET_RCFACTORYRES,
  ARTNET_RCMAXCODE
} artnet_node_report_code_en;

typedef enum
{
  ARTNET_ACNONE = 0x0,        // no action
  ARTNET_ACCANCELMERGE,       // if node currently in merge mode, cancel merge mode after next ArtDmx packet
  ARTNET_ACLEDNORMAL,         // normal front panel indicators
  ARTNET_ACLEDMUTE,           // muted front panel indicators
  ARTNET_ACLEDLOCATE,         // rapid flash of front panel indicators
  ARTNET_ACRESETRX,           // reset sip text test and data error flags, forces test to re-run

  ARTNET_ACMERGELTP0 = 0x10,  // set DMX port 0 to merge in LTP
  ARTNET_ACMERGELTP1 = 0x11,
  ARTNET_ACMERGELTP2 = 0x12,
  ARTNET_ACMERGELTP3 = 0x13,

  ARTNET_ACMERGEHTP0 = 0x50,  // set DMX port 0 to merge in HTP (default)
  ARTNET_ACMERGEHTP1 = 0x51,
  ARTNET_ACMERGEHTP2 = 0x52,
  ARTNET_ACMERGEHTP3 = 0x53,

  ARTNET_ACARTNETSEL0 = 0x60, // set DMX port 0 to output DMX512 and RDM from ArtNet Protocol (default)
  ARTNET_ACARTNETSEL1 = 0x61,
  ARTNET_ACARTNETSEL2 = 0x62,
  ARTNET_ACARTNETSEL3 = 0x63,

  ARTNET_ACACNSEL0 = 0x70,    // set DMX port 0 to output DMX512 from the sACN protocol and RDM from ArtNet protocol
  ARTNET_ACACNSEL1 = 0x71,
  ARTNET_ACACNSEL2 = 0x72,
  ARTNET_ACACNSEL3 = 0x73,

  ARTNET_ACCLEAROP0 = 0x90,   // clear DMX output buffer for port 0
  ARTNET_ACCLEAROP1 = 0x91,
  ARTNET_ACCLEAROP2 = 0x92,
  ARTNET_ACCLEAROP3 = 0x93

} artnet_node_address_command_en;

typedef enum
{
  STNODE,
  STCONTROLLER,
  STMEDIA,
  STROUTE,
  STBACKUP,
  STCONFIG,
  STVISUAL
} artnet_node_style_code_en;

typedef enum
{
  ARTNET_OPCODE_POLL              = 0x2000,
  ARTNET_OPCODE_REPLY             = 0x2100,
  ARTNET_OPCODE_DIAGDATA          = 0x2300,
  ARTNET_OPCODE_COMMAND           = 0x2400,
  ARTNET_OPCODE_DMX               = 0x5000,
  ARTNET_OPCODE_NZS               = 0x5100,
  ARTNET_OPCODE_SYNC              = 0x5200,
  ARTNET_OPCODE_ADDRESS           = 0x6000,
  ARTNET_OPCODE_INPUT             = 0x7000,
  ARTNET_OPCODE_TODREQUEST        = 0x8000,
  ARTNET_OPCODE_TODDATA           = 0x8100,
  ARTNET_OPCODE_TODCONTROL        = 0x8200,
  ARTNET_OPCODE_RDM               = 0x8300,
  ARTNET_OPCODE_RDMSUB            = 0x8400,
  ARTNET_OPCODE_VIDEOSTEUP        = 0xa010,
  ARTNET_OPCODE_VIDEOPALETTE      = 0xa020,
  ARTNET_OPCODE_VIDEODATA         = 0xa040,
  ARTNET_OPCODE_MACMASTER         = 0xf000,
  ARTNET_OPCODE_MACSLAVE          = 0xf100,
  ARTNET_OPCODE_FIRMWAREMASTER    = 0xf200,
  ARTNET_OPCODE_FIRMWAREREPLY     = 0xf300,
  ARTNET_OPCODE_FILETNMASTER      = 0xf400,
  ARTNET_OPCODE_FILEFNMASTER      = 0xf500,
  ARTNET_OPCODE_FILEFNREPLY       = 0xf600,
  ARTNET_OPCODE_IPPROG            = 0xf800,
  ARTNET_OPCODE_IPREPLY           = 0xf900,
  ARTNET_OPCODE_MEDIA             = 0x9000,
  ARTNET_OPCODE_MEDIAPATCH        = 0x9100,
  ARTNET_OPCODE_MEDIACONTROL      = 0x9200,
  ARTNET_OPCODE_MEDIACONTROLREPLY = 0x9300,
  ARTNET_OPCODE_TIMECODE          = 0x9700,
  ARTNET_OPCODE_TIMESYNC          = 0x9800,
  ARTNET_OPCODE_TRIGGER           = 0x9900,
  ARTNET_OPCODE_DIRECTORY         = 0x9a00,
  ARTNET_OPCODE_DIRECTORYREPLY    = 0x9b00
} artnet_packet_en;

typedef struct
{
  uint8_t ubea_present:1;
  uint8_t dual_boot:1;
  uint8_t nu:1;
  uint8_t papa:3;     // Port Address Programming Authority
  uint8_t led_state:2;  //0: unknown, 1: locate mode, 2: mute mode, 3: normal mode
} tp_artnet_status1;

typedef struct
{
  uint8_t has_web:1;
  uint8_t dhcp_enabled:1;
  uint8_t has_dhcp:1;
  uint8_t artnet3:1;
  uint8_t sacn_able:1;
  uint8_t squawking:1;
  uint8_t nu:2;
} tp_artnet_status2;

typedef struct
{
  uint8_t has_output:1;
  uint8_t has_input:1;
  uint8_t protocol:6;
} tp_artnet_porttype;

// Big union with all possible artnet
// structs we might need
typedef union artnet_packet_u
{
  // Header
  struct artnet_header_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
  } __attribute__((packed)) header;
 
  // Sync
  struct artnet_sync_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint16_t    prot_ver;
    uint8_t     aux1;
    uint8_t     aux2;
  } __attribute__((packed)) sync;
  
  // Poll
  struct artnet_poll_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     talk_to_me;
    uint8_t     priority;
  } __attribute__((packed)) poll;

  // Poll reply
  struct artnet_pollreply_t
  {
    uint8_t  id[8];
    uint16_t opCode;
    uint32_t ip;
    uint16_t port;
    uint16_t ver;
    uint8_t  net;
    uint8_t  sub;
    uint16_t oem;
    uint8_t  ubea;
    uint8_t  status;
    uint16_t estaCode;
    uint8_t  shortName[ARTNET_SHORT_NAME_LENGTH];
    uint8_t  longName[ARTNET_LONG_NAME_LENGTH];
    uint8_t  nodereport[ARTNET_REPORT_LENGTH];
    uint16_t numbports;
    uint8_t  portTypes[4];
    uint8_t  inputStatus[4];
    uint8_t  outputStatus[4];
    uint8_t  inputSubswitch[4];
    uint8_t  outputSubswitch[4];
    uint8_t  swvideo;
    uint8_t  swmacro;
    uint8_t  swremote;
    uint8_t  sp1;
    uint8_t  sp2;
    uint8_t  sp3;
    uint8_t  style;
    uint8_t  mac[6];
    uint32_t bindIp;
    uint8_t  bindIndex;
    uint8_t  status2;
    uint8_t  filler[26];
  } __attribute__((packed)) pollreply;

  // IP Prog
  struct artnet_ipprog_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     nu1[2];
    uint8_t     command;
    uint8_t     nu2;
    uint8_t     ip[4];
    uint8_t     subnet[4];
    uint16_t    port;
    uint8_t     nu3[8];
  } __attribute__((packed)) ipprog;

  // IP Prog Reply
  struct artnet_ipprog_reply_tp
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     nu1[4];
    uint32_t    ip;
    uint32_t    subnet;
    uint16_t    port;
    uint8_t     status;
    uint8_t     nu2[7];
  } __attribute__((packed)) ipprogreply;

  // DMX
  struct artnet_dmx_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     seq;        // The sequence number is used to ensure that ArtDmx packets are used in the correct order.
                            //When Art-Net is carried over a medium such as the Internet,
                            //it is possible that ArtDmx packets will reach the receiver out of order.
                            //This field is incremented in the range 0x01 to 0xff to allow the receiving node
                            //to resequence packets.
                            //The Sequence field is set to 0x00 to disable this feature.
    uint8_t     physical;   // The physical input port from which DMX512 data was input.
    uint8_t     sub_uni;    // The low byte of the 15 bit Port-Address to which this packet is destined.
    uint8_t     net;        // The top 7 bits of the 15 bit Port-Address to which this packet is destined.
    uint16_t    length;     // The length of the DMX512 data array. This value should be an even number in the range 2  512.
    uint8_t     data[];     // pointer to data
  } __attribute__((packed)) dmx;
  
  // Art Address
  struct artnet_address_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     net;
    uint8_t     bindIndex;
    uint8_t     short_name[18];
    uint8_t     long_name[64];
    uint8_t     swin[4];
    uint8_t     swout[4];
    uint8_t     sub;
    uint8_t     swvideo;
    uint8_t     command;
  } __attribute__((packed)) address;

  // ToD Request
  struct artnet_todrequest_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     filler[2];
    uint8_t     spare[7];
    uint8_t     net;
    uint8_t     command;
    uint8_t     addcount;
    uint8_t     address[32];
  } __attribute__((packed)) todrequest;

  // ToD Control
  struct artnet_todcontrol_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     filler[2];
    uint8_t     spare[7];
    uint8_t     net;
    uint8_t     command;
    uint8_t     address;
  } __attribute__((packed)) todcontrol;

  // ToD Data
  struct artnet_toddata_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     rdmver;
    uint8_t     port;
    uint8_t     spare[6];
    uint8_t     bindIndex;
    uint8_t     net;
    uint8_t     cmdResponse;
    uint8_t     address;
    uint8_t     uidTotalHi;
    uint8_t     uidTotalLo;
    uint8_t     blockCount;
    uint8_t     uidCount;
    uint8_t     *tod[6];
  } __attribute__((packed)) toddata;

  // Art RDM
  struct artnet_rdm_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     rdmver;
    uint8_t     filler;
    uint8_t     spare[7];
    uint8_t     net;
    uint8_t     command;
    uint8_t     address;
    uint8_t     *rdmpacket;
  } __attribute__((packed)) rdm;

  // Art RDM Sub
  struct artnet_rdmsub_t
  {
    uint8_t     id[8];
    uint16_t    opCode;
    uint8_t     prot_ver_hi;
    uint8_t     prot_ver_low;
    uint8_t     rdmver;
    uint8_t     filler;
    uint8_t     uid;
    uint8_t     spare;
    uint8_t     cmdClass;
    uint8_t     paramId;
    uint8_t     subDevice;
    uint8_t     subCount;
    uint8_t     spare2[4];
    uint16_t    *data;
  } __attribute__((packed)) rdmsub;

  uint8_t raw[580];
} artnet_packet_u;

typedef union {
  struct {
    struct { /* ACN Root Layer: 38 bytes */
      uint16_t preamble_size;    /* Preamble Size */
      uint16_t postamble_size;   /* Post-amble Size */
      uint8_t  acn_pid[12];      /* ACN Packet Identifier */
      uint16_t flength;          /* Flags (high 4 bits) & Length (low 12 bits) */
      uint32_t vector;           /* Layer Vector */
      uint8_t  cid[16];          /* Component Identifier (UUID) */
    } __attribute__((packed)) root;

    struct { /* Framing Layer: 77 bytes */
      uint16_t flength;          /* Flags (high 4 bits) & Length (low 12 bits) */
      uint32_t vector;           /* Layer Vector */
      uint8_t  source_name[64];  /* User Assigned Name of Source (UTF-8) */
      uint8_t  priority;         /* Packet Priority (0-200, default 100) */
      uint16_t reserved;         /* Reserved (should be always 0) */
      uint8_t  seq_number;       /* Sequence Number (detect duplicates or out of order packets) */
      uint8_t  options;          /* Options Flags (bit 7: preview data, bit 6: stream terminated) */
      uint16_t universe;         /* DMX Universe Number */
    } __attribute__((packed)) frame;

    struct { /* Device Management Protocol (DMP) Layer: 523 bytes */
      uint16_t flength;          /* Flags (high 4 bits) / Length (low 12 bits) */
      uint8_t  vector;           /* Layer Vector */
      uint8_t  type;             /* Address Type & Data Type */
      uint16_t first_addr;       /* First Property Address */
      uint16_t addr_inc;         /* Address Increment */
      uint16_t prop_val_cnt;     /* Property Value Count (1 + number of slots) */
      uint8_t  prop_val[513];    /* Property Values (DMX start code + slots data) */
    } __attribute__((packed)) dmp;
  } __attribute__((packed));

  uint8_t raw[638]; /* raw buffer view: 638 bytes */
} e131_packet_t;

// Callbacks

typedef void (*groupDmxCallback_t)(uint8_t port, uint16_t len, uint8_t *data);
typedef void (*groupRdmCallback_t)(uint8_t port, uint16_t len, uint8_t *data);

/**
 * Struct defining our artnet groups
 * 
 * a group is a collection of 4 ports
 * each that can receive or send dmx
 * or rdm.
 */

typedef struct artnet_group_t
{
  uint8_t ports;
  uint8_t net;
  uint8_t subnet;
  uint8_t seq;
  uint8_t portType[4];
  uint8_t inputStatus[4];
  uint8_t outputStatus[4];
  uint8_t swin[4];
  uint8_t swout[4];
  groupDmxCallback_t dmxcb;
  groupRdmCallback_t rdmcb;
} artnet_group_t;

/**
 * Struct used to config our artnet node
 *
 * This info is all it's necessary to
 * operate this node.
 *
 */

typedef struct
{
  ustack_iface_t *iface;
  
  struct { ioportid_t port; uint8_t pad; } ledGreen;
  struct { ioportid_t port; uint8_t pad; } ledRed;

  uint16_t port;          // Artnet Port
  uint16_t sacnPort;      // sacn Port

  uint8_t shortName[18];  // Node shortname 
  uint8_t longName[64];   // Node longname

  bool rdmEnabled;        // Does this node support RDM ?
  bool sacnEnabled;       // Is sACN enabled ?

  artnet_group_t groups[ARTNET_GROUPS];
} artnet_config_t;

/**
 * struct holding the artnet status
 *
 * instead of having global variables lying around
 * let's keep it tidy all inside a struct.
 */
typedef struct
{
  artnet_config_t *cfg;            // Artnet Node Config
  
  struct uip_udp_conn *artnetConn; // ArtNet connection
  struct uip_udp_conn *sacnConn;   // sACN connection

  uint16_t pollCount;              // ArtPoll count
  systime_t lastDmxPacket;          // time of last DMX Packet
  uint32_t lastIpSrc;         // The IP of the first DMX packet
  
  thread_t *locateThread;          // Thread pointer to our led blink thread

  uint8_t statusLeds;              // LED Status

  uint8_t reportCode;              // Report code
  uint8_t report[64];              // String holding the report text
} artnet_status_t;

void artnetInit(artnet_config_t *cfg);
void artnetParser(ustack_iface_t *iface, uint16_t len);
void sacnParser(ustack_iface_t *iface, uint16_t len);
void artnetSetGroupDmxCallback(uint8_t grp, groupDmxCallback_t cb);
void artnetSetGroupRdmCallback(uint8_t grp, groupRdmCallback_t cb);
void artnetSendFirstPollReply(ustack_iface_t *iface);

#endif
