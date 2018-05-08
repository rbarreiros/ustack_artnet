#include <artnet.h>
#include <ustack.h>
#include <ustack_thread.h>
#include <ustack_udp.h>

#include <ch.h>
#include <hal.h>
#include <string.h>

// Debug
#include "serial.h"

// Struct holding all artnet globals, status, etc
artnet_status_t gArtStatus = {0};

// Constants, report code text, hopefully goes into flash!!
static const char * const gReportCodeTable[] =
{
  "Booted in debug mode.",
  "Power On Tests successful.",
  "Hardware tests failed at Power On.",
  "Last UDP from Node failed due to truncated length.",
  "Unable to identify last UDP transmission.",
  "Unable to open Udp Socket in last transmission attempt.",
  "Short Name programming via ArtAddress successful.",
  "Long Name programming via ArtAddress successful.",
  "DMX512 receive errors detected.",
  "Ran out of internal DMX transmit buffers.",
  "Ran out of internal DMX Rx buffers.",
  "Rx Universe switches conflict.",
  "Product configuration does not match firmware.",
  "DMX output short detected.",
  "Last attempt to upload new firmware failed.",
  "User changed switch settings when address locked by remote programming.",
  "Factory reset has occurred."
};

/*******************************************/
/* PRIVATE FUNCTIONS                       */
/*******************************************/

/**
 * Prints a number in hex as a string
 *
 * unsigned long num - Number to be converted
 * int base          - base, 10, 16
 * char sign         - signed or unsigned (char, +, -)
 * char *outbuf      - buffer holding the output
 */
static void artnetPrntnum(unsigned long num, int base, char sign, char *outbuf)
{
  int i = 12;
  int j = 0;

  do {
    outbuf[i] = "0123456789ABCDEF"[num % base];
    i--;
    num = num/base;
  } while (num > 0);

  if(sign != ' ')
  {
    outbuf[0] = sign;
    ++j;
  }

  while( ++i < 13)
  {
    outbuf[j++] = outbuf[i];
  }
}

/**
 * Get ArtNet default IP
 *
 * Calculates the artnet default IP based on
 * mac address.
 *
 * bool v4         - use v4 spec ip addres or not
 *                   v4 uses 10.x.x.x others 2.x.x.x
 */
static uint32_t artnetDefaultIp(bool v4)
{
  return ustackIpToA((v4 == true) ? 10 : 2,
                     gArtStatus.cfg->iface->cfg->mac[3] + OEM,
                     gArtStatus.cfg->iface->cfg->mac[4],
                     gArtStatus.cfg->iface->cfg->mac[5]);
}

/**
 * Returns the default netmask
 * which is 255.0.0.0
 *
 * uip_ipaddr_t nm - where the netmask will be stored
 */
static uint32_t artnetDefaultNetmask(void)
{
  return ustackIpToA(255, 0, 0, 0);
}

/**
 * Return the text referring to the report code
 *
 * uint8_t reportcode - the report code
 * char report        - the text
 *
 */
/*
static void artnetGetReportText(uint8_t reportCode, char *report)
{
  if(reportCode >= ARTNET_RCMAXCODE) return;
  memcpy(report, gReportCodeTable[reportCode], ARTNET_REPORT_LENGTH);
}
*/

/**
 * Thread to blink leds
 *
 */
static THD_FUNCTION(LocateThread, arg)
{
  (void)arg;
  chRegSetThreadName("Locate Thread");

  while (!chThdShouldTerminateX())
  {
    palTogglePad(gArtStatus.cfg->ledGreen.port, gArtStatus.cfg->ledGreen.pad);
    palTogglePad(gArtStatus.cfg->ledRed.port, gArtStatus.cfg->ledRed.pad);
    chThdSleepMilliseconds(250);
  }
  
  return;
}

/**
 * Switches the LED's status into 
 * normal operation
 *
 */
static void artnetSetLedsNormal(void)
{
  if(gArtStatus.locateThread != NULL)
  {
    chThdTerminate(gArtStatus.locateThread);
    chThdWait(gArtStatus.locateThread);
    gArtStatus.locateThread = NULL;
  }

  palSetPad(gArtStatus.cfg->ledGreen.port, gArtStatus.cfg->ledGreen.pad);
  palClearPad(gArtStatus.cfg->ledRed.port, gArtStatus.cfg->ledRed.pad);

  gArtStatus.statusLeds = ARTNET_STATUS_INDICATOR_NORMAL;
}

/**
 * Switches the LED's status into
 * mute state
 *
 */
static void artnetSetLedssMute(void)
{
  if(gArtStatus.locateThread != NULL)
  {
    chThdTerminate(gArtStatus.locateThread);
    chThdWait(gArtStatus.locateThread);
    gArtStatus.locateThread = NULL;
  }

  palClearPad(gArtStatus.cfg->ledGreen.port, gArtStatus.cfg->ledGreen.pad);
  palClearPad(gArtStatus.cfg->ledRed.port, gArtStatus.cfg->ledRed.pad);

  gArtStatus.statusLeds = ARTNET_STATUS_INDICATOR_MUTE;
}

/**
 * Enables LED locate mode blinking
 * both led's
 *
 */
static void artnetSetLedsLocate(void)
{
  // spawn a heap thread with them blinking
  if(gArtStatus.locateThread == NULL)
  {
    palSetPad(gArtStatus.cfg->ledGreen.port, gArtStatus.cfg->ledGreen.pad);
    palSetPad(gArtStatus.cfg->ledRed.port, gArtStatus.cfg->ledRed.pad);
    gArtStatus.locateThread = chThdCreateFromHeap(NULL,
                                                  THD_WORKING_AREA_SIZE(128),
                                                  "LocateThread",
                                                  NORMALPRIO - 1,
                                                  LocateThread, NULL);
  }

  gArtStatus.statusLeds = ARTNET_STATUS_INDICATOR_LOCATE;
}

/**
 * Restarts ethernet, to apply
 * ethernet config changes
 *
 */
static void artnetRestart(uint32_t ip, uint32_t nm, uint16_t port)
{
  dbg(":: ARTNET :: Restarting Ethernet");
  
  if(port != gArtStatus.cfg->port)
  {
    ustackUdpRemoveListener(gArtStatus.cfg->port);
    gArtStatus.cfg->port = port;
  }

  if(ip != 0 && ip != gArtStatus.cfg->iface->cfg->ip)
    gArtStatus.cfg->iface->cfg->ip = ip;

  if(nm != 0 && nm != gArtStatus.cfg->iface->cfg->netmask)
    gArtStatus.cfg->iface->cfg->netmask = nm;
  
  gArtStatus.pollCount = 0;
  gArtStatus.lastDmxPacket = 0;
  gArtStatus.lastIpSrc = 0;

  artnetSetLedsNormal();
  
  gArtStatus.reportCode = ARTNET_RCPOWEROK;

  // Artnet
  dbgf(":: ARTNET :: Binding Artnet to Port: %d\r\n", gArtStatus.cfg->port);
  ustackUdpAddListener(gArtStatus.cfg->port, artnetParser);

  if(gArtStatus.cfg->sacnEnabled)
  {
    // sACN
    dbgf(":: ARTNET :: Binding sACN to Port: %d\r\n", gArtStatus.cfg->sacnPort);
    ustackUdpAddListener(gArtStatus.cfg->sacnPort, sacnParser);
  }
}

/**
 * builds a node report code
 *
 * uint8_t report - the output holding the report code string
 *
 */
static void artnetBuildReportCode(uint8_t *report)
{
  char tmp[4] = { '0', '0', '0', '0' };

  memset(gArtStatus.report, 0, ARTNET_REPORT_LENGTH);
  
  if(gArtStatus.reportCode < ARTNET_RCMAXCODE)
    artnetPrntnum(gArtStatus.reportCode, 16, ' ', tmp);

  report[0] = '#';
  report[1] = '0';
  report[2] = '0';
  report[3] = (gArtStatus.reportCode > 0xf) ? tmp[0] : '0';
  report[4] = (gArtStatus.reportCode > 0xf) ? tmp[1] : tmp[0];

  tmp[0] = '0'; tmp[1] = '0'; tmp[2] = '0'; tmp[3] = '0';
  artnetPrntnum(gArtStatus.pollCount, 10, ' ', tmp);

  report[5] = '[';

  if(gArtStatus.pollCount < 10)  { report[6] = tmp[3]; report[7] = tmp[2]; report[8] = tmp[1]; report[9] = tmp[0]; }
  if(gArtStatus.pollCount > 9)   { report[6] = tmp[3]; report[7] = tmp[2]; report[8] = tmp[0]; report[9] = tmp[1]; }
  if(gArtStatus.pollCount > 99)  { report[6] = tmp[3]; report[7] = tmp[0]; report[8] = tmp[1]; report[9] = tmp[2]; }
  if(gArtStatus.pollCount > 999) { report[6] = tmp[0]; report[7] = tmp[1]; report[8] = tmp[2]; report[9] = tmp[3]; }

  report[10] = ']';

  memcpy(&report[11], gReportCodeTable[gArtStatus.reportCode], ARTNET_REPORT_LENGTH - 11);
}

/**
 * Clear a DMX ouput setting all channels at 0
 *
 * artnet_group_t grp - the group of the port
 * uint8_t port       - the port number
 */
static bool artnetClearDmxOutput(artnet_group_t *grp, uint8_t port)
{
  if(port >= grp->ports) return false;

  uint8_t tmp[512] = {0};
  grp->dmxcb(port, 512, tmp);

  return true;
}

/**
 * ArtPollReply
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * All Devices      | Receive               | No action.
 *                  | Unicast Transmit      | Not Allowed.
 *                  | Directed Broadcast    | Directed broadcast this packet in response
 *                                            to an ArtPoll
 * --------------------------------------------------------------------------------------
 *
 * A device, in response to a Controller’s ArtPoll, sends the ArtPollReply. This packet 
 * is also broadcast to the Directed Broadcast address by all Art-Net devices on power up.
 *
 */
static void artnetSendPollReply(artnet_packet_u *artnet)
{
  uint8_t i;

  memcpy(artnet->pollreply.id, "Art-Net\0", 8);
  artnet->pollreply.opCode = ARTNET_OPCODE_REPLY;
  artnet->pollreply.ip = htonl(gArtStatus.cfg->iface->cfg->ip);

  artnet->pollreply.port = gArtStatus.cfg->port;
  artnet->pollreply.ver = VERSION;
  artnet->pollreply.oem = htons(ARTNET_OEM);
  artnet->pollreply.ubea = 0;
  
  artnet->pollreply.status = gArtStatus.statusLeds | ARTNET_STATUS_PROG_NETWORK;

  if(gArtStatus.cfg->rdmEnabled)
    artnet->pollreply.status |= ARTNET_STATUS_RDM_ENABLED;

  artnet->pollreply.estaCode = 0;
  memcpy(artnet->pollreply.shortName, gArtStatus.cfg->shortName, ARTNET_SHORT_NAME_LENGTH);
  memcpy(artnet->pollreply.longName, gArtStatus.cfg->longName, ARTNET_LONG_NAME_LENGTH);

  artnetBuildReportCode(artnet->pollreply.nodereport);

  artnet->pollreply.swvideo = 0;
  artnet->pollreply.swmacro = 0;
  artnet->pollreply.swremote = 0;

  artnet->pollreply.sp1 = artnet->pollreply.sp2 = artnet->pollreply.sp3 = 0;

  artnet->pollreply.style = STNODE;
  memcpy(artnet->pollreply.mac, gArtStatus.cfg->iface->cfg->mac, 6);
  artnet->pollreply.bindIp = artnet->pollreply.ip;

  artnet->pollreply.status2 = ARTNET_STATUS2_ARTNETV4;

  if(gArtStatus.cfg->sacnEnabled)
    artnet->pollreply.status2 |= ARTNET_STATUS2_HAS_SACN;
  
  memset(artnet->pollreply.filler, 0, 26);

  for(i = 0; i < ARTNET_GROUPS; i++)
  {
    uint8_t j;
    artnet_group_t *grp = &gArtStatus.cfg->groups[i];

    artnet->pollreply.net = grp->net;
    artnet->pollreply.sub = grp->subnet;
    
    artnet->pollreply.numbports = htons(grp->ports);
    for(j = 0; j < 4; j++)
    {
      artnet->pollreply.portTypes[j] = grp->portType[j];
      artnet->pollreply.inputStatus[j] = grp->inputStatus[j];
      artnet->pollreply.outputStatus[j] = grp->outputStatus[j];
      artnet->pollreply.inputSubswitch[j] = grp->swin[j];
      artnet->pollreply.outputSubswitch[j] = grp->swout[j];
    }
  
    // which group this reply belongs to
    artnet->pollreply.bindIndex = i;
    
    // Send it for each group
    uint8_t bcastMac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    ustackUdpSend(gArtStatus.cfg->iface,
                  bcastMac,
                  ustackGetDirectedBroadcast(gArtStatus.cfg->iface->cfg->ip,
                                             gArtStatus.cfg->iface->cfg->netmask),
                  gArtStatus.cfg->port, gArtStatus.cfg->port,
                  sizeof(struct artnet_pollreply_t));

    gArtStatus.pollCount++;
    if(gArtStatus.pollCount > 9999)
      gArtStatus.pollCount = 0;
  }
}

/**
 * ArtIpProg
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No action.
 *                  | Unicast Transmit      | Controller transmits to a specific node IP address.
 *                  | Directed Broadcast    | No allowed.
 * --------------------------------------------------------------------------------------
 * Node             | Receive               | Reply by broadcasting ArtIpProgReply.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | Reply by broadcasting ArtIpProgReply.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 *
 * The ArtIpProg packet allows the IP settings of a Node to be reprogrammed.
 * The ArtIpProg packet is sent by a Controller to the private address of a Node. 
 * If the Node supports remote programming of IP address, it will respond with an 
 * ArtIpProgReply packet.
 * In all scenarios, the ArtIpProgReply is sent to the private address of the sender.
 *
 */
static void artnetHandleIPProg(artnet_packet_u *artnet)
{
  bool restartNic = false;
  uint32_t ip = 0;
  uint32_t nm = 0;
  uint16_t aport = 0;
  
  if(artnet->ipprog.command != 0)
  {
    if(artnet->ipprog.command & ARTNET_IPPROG_ENABLE)
    {
      if(artnet->ipprog.command & ARTNET_IPPROG_DEF)
      {
        // Set defaults
        ip = artnetDefaultIp(true);
        nm = artnetDefaultNetmask();
        gArtStatus.cfg->port = ARTNET_PORT;
        gArtStatus.reportCode = ARTNET_RCFACTORYRES;
        restartNic = true;
      }
      else
      {
        if(artnet->ipprog.command & ARTNET_IPPROG_IP)
        {
          // Changed IP
          ip = ntohl(artnet->ipprog.ip);
          restartNic = true;
        }

        if(artnet->ipprog.command & ARTNET_IPPROG_SUB)
        {
          // Changed netmask
          nm = ntohl(artnet->ipprog.subnet);
          restartNic = true;
        }

        if(artnet->ipprog.command & ARTNET_IPPROG_PORT)
        {
          // Changed Port
          aport = htons(artnet->ipprog.port);
          restartNic = true;
        }
      }
      
    }
  }
  
  memcpy(artnet->ipprogreply.id, "Art-Net\0", 8);
  artnet->ipprogreply.opCode = ARTNET_OPCODE_IPREPLY;
  artnet->ipprogreply.prot_ver_hi = ARTNET_VERSION;
  artnet->ipprogreply.prot_ver_low = 0;
  memset(artnet->ipprogreply.nu1, 0, 4);
  artnet->ipprogreply.ip = ip;
  artnet->ipprogreply.subnet = nm;
  artnet->ipprogreply.port = htons(gArtStatus.cfg->port);
  artnet->ipprogreply.status = 0;
  memset(artnet->ipprogreply.nu2, 0, 7);

  ustackUdpSend(gArtStatus.cfg->iface,
                NULL,
                0,
                gArtStatus.cfg->port, gArtStatus.cfg->port,
                sizeof(struct artnet_ipprog_reply_tp));
  
  if(restartNic)
  {
    chThdSleepMilliseconds(1000);
    artnetRestart(ip, nm, aport);
  }
}

/**
 * ArtAddress
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No action.
 *                  | Unicast Transmit      | Controller transmits to a specific node IP address.
 *                  | Directed Broadcast    | No allowed.
 * --------------------------------------------------------------------------------------
 * Node             | Receive               | Reply by broadcasting ArtPollReply.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | Reply by broadcasting ArtPollReply.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 *
 * A Controller or monitoring device on the network can reprogram numerous controls of a
 * node remotely. This, for example, would allow the lighting console to re-route DMX512 
 * data at remote locations. This is achieved by sending an ArtAddress packet to the Node’s 
 * IP address. (The IP address is returned in the ArtPoll packet). The node replies with an 
 * ArtPollReply packet.
 *
 * Fields 5 to 13 contain the data that will be programmed into the node
 *
 */
static void artnetHandleAddress(artnet_packet_u *artnet)
{
  uint8_t i, group;

  group = artnet->address.bindIndex;

  if(artnet->address.short_name[0] != 0)
    memcpy(gArtStatus.cfg->shortName, artnet->address.short_name, ARTNET_SHORT_NAME_LENGTH);

  if(artnet->address.long_name[0] != 0)
    memcpy(gArtStatus.cfg->longName, artnet->address.long_name, ARTNET_LONG_NAME_LENGTH);

  // Find the group this artaddress belongs to
  if(group > ARTNET_GROUPS)
    group = 0;
  
  artnet_group_t *grp = &gArtStatus.cfg->groups[group];

  if(grp != NULL)
  {
    if (artnet->address.net & 0x80)
      grp->net = artnet->address.net & 0x7f;
    else if(artnet->address.net == 0)
      grp->net = 0;
    
    if (artnet->address.sub & 0x80)
      grp->subnet = artnet->address.sub & 0x7f;
    else if(artnet->address.sub == 0)
      grp->subnet = 0;
    
    // Process only the amount of ports in the group
    // ignore the rest
    for(i = 0; i < grp->ports; i++)
    {
      if(artnet->address.swin[i] & 0x80)
        grp->swin[i] = artnet->address.swin[i] & 0x7f;
      else if(artnet->address.swin[i] == 0) // Default ( group id + port )
        grp->swin[i] = group + i;

      if(artnet->address.swout[i] & 0x80)
        grp->swout[i] = artnet->address.swout[i] & 0x7f;
      else if(artnet->address.swout[i] == 0) // Default ( group id + port )
        grp->swout[i] = group + i;
    }

    switch(artnet->address.command)
    {
        // AcNone
      case ARTNET_ACNONE:
        break;

        // AcCancelMerge
      case ARTNET_ACCANCELMERGE:
        break;
        
        // AcLedNormal
      case ARTNET_ACLEDNORMAL:
        artnetSetLedsNormal();
        break;
        // AcLedMute
      case ARTNET_ACLEDMUTE:
        artnetSetLedssMute();
        break;
        // AcLedLocate
      case ARTNET_ACLEDLOCATE:
        artnetSetLedsLocate();
        break;

        // AcResetRx Flags
      case ARTNET_ACRESETRX:
        break;
        
        // Merge ---- we do not support merging !
        // AcMergeLtp0
      case ARTNET_ACMERGELTP0:
        //grp->outputStatus[0] |= ARTNET_OUTPUT_LTP;
        break;
        // AcMergeLtp1
      case ARTNET_ACMERGELTP1:
        //grp->outputStatus[1] |= ARTNET_OUTPUT_LTP;
        break;
        // AcMergeLtp2
      case ARTNET_ACMERGELTP2:
        //grp->outputStatus[2] |= ARTNET_OUTPUT_LTP;
        break;
        // AcMergeLtp3
      case ARTNET_ACMERGELTP3:
        //grp->outputStatus[3] |= ARTNET_OUTPUT_LTP;
        break;

        // AcMergeHtp0
      case ARTNET_ACMERGEHTP0: 
        //grp->outputStatus[0] &= ~ARTNET_OUTPUT_LTP;
        break;
        // AcMergeHtp1
      case ARTNET_ACMERGEHTP1: 
        //grp->outputStatus[1] &= ~ARTNET_OUTPUT_LTP;
        break;
        // AcMergeHtp2
      case ARTNET_ACMERGEHTP2: 
        //grp->outputStatus[2] &= ~ARTNET_OUTPUT_LTP;
        break;
        // AcMergeHtp3
      case ARTNET_ACMERGEHTP3: 
        //grp->outputStatus[3] &= ~ARTNET_OUTPUT_LTP;
        break;

      // Output

        // AcArtnetSel0
      case ARTNET_ACARTNETSEL0:
        grp->outputStatus[0] = 0;
        break;
        // AcArtnetSel1
      case ARTNET_ACARTNETSEL1:
        grp->outputStatus[1] = 0;
        break;
        // AcArtnetSel2
      case ARTNET_ACARTNETSEL2:
        grp->outputStatus[2] = 0;
        break;
        // AcArtnetSel3
      case ARTNET_ACARTNETSEL3:
        grp->outputStatus[3] = 0;
        break;

        // AcAcnSel0
      case ARTNET_ACACNSEL0:
        grp->outputStatus[0] |= ARTNET_OUTPUT_SACN;
        break;
        // AcAcnSel1
      case ARTNET_ACACNSEL1:
        grp->outputStatus[1] |= ARTNET_OUTPUT_SACN;
        break;
        // AcAcnSel2
      case ARTNET_ACACNSEL2:
        grp->outputStatus[2] |= ARTNET_OUTPUT_SACN;
        break;
        // AcAcnSel3
      case ARTNET_ACACNSEL3:
        grp->outputStatus[3] |= ARTNET_OUTPUT_SACN;
        break;
        
        // Clear outputs
        // AcClearOp0
      case ARTNET_ACCLEAROP0:
        artnetClearDmxOutput(grp, 0);
        break;
        // AcClearOp1
      case ARTNET_ACCLEAROP1:
        artnetClearDmxOutput(grp, 1);
        break;
        // AcClearOp2
      case ARTNET_ACCLEAROP2:
        artnetClearDmxOutput(grp, 2);
        break;
        // AcClearOp3
      case ARTNET_ACCLEAROP3:
        artnetClearDmxOutput(grp, 3);
        break;
    };
  }
  
  artnetSendPollReply(artnet);
}

/**
 * ArtDmx
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | Application specific.
 *                  | Unicast Transmit      | Yes
 *                  | Directed Broadcast    | No
 * --------------------------------------------------------------------------------------
 * Node             | Receive               | Application specific
 *                  | Unicast Transmit      | Yes
 *                  | Broadcast             | No
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | Application specific.
 *                  | Unicast Transmit      | Yes
 *                  | Broadcast             | No
 * --------------------------------------------------------------------------------------
 *
 * ArtDmx is the data packet used to transfer DMX512 data. The format is identical for 
 * Node to Controller, Node to Node and Controller to Node.
 * The Data is output through the DMX O/P port corresponding to the Universe setting. In 
 * the absence of received ArtDmx packets, each DMX O/P port re-transmits the same 
 * frame continuously.
 *
 * The first complete DMX frame received at each input port is placed in an ArtDmx packet 
 * as above and transmitted as an ArtDmx packet containing the relevant Universe parameter.
 * Each subsequent DMX frame containing new data (different length or different contents) 
 * is also transmitted as an ArtDmx packet.
 *
 * Nodes do not transmit ArtDmx for DMX 512 inputs that have not received data since power on.
 * However, an input that is active but not changing, will re-transmit the last valid ArtDmx 
 * packet at approximately 4-second intervals.
 *
 * (Note. In order to converge the needs of Art-Net and sACN it is recommended that Art-Net 
 * devices actually use a re-transmit time of 800mS to 1000mS).
 *
 * A DMX input that fails will not continue to transmit ArtDmx data.
 *
 * Unicast Subscription:
 * ---------------------
 *
 * ArtDmx packets must be unicast to subscribers of the specific universe contained in
 * the ArtDmx packet. 
 * The transmitting device must regularly ArtPoll the network to detect any change in 
 * devices which are subscribed. Nodes that are subscribed will list the subscription 
 * universe in the ArtPollReply. 
 * Subscribed means any universes listed in either the Swin orSwout array.
 * If there are no subscribers to a universe that the transmitter wishes to send, then the 
 * ArtDmx must not be broadcast.
 * If the number of universe subscribers exceeds 40 for a given universe, the transmitting 
 * device may broadcast.
 *
 * Refresh Rate:
 * -------------
 * The ArtDmx packet is intended to transfer DMX512 data. For this reason, the ArtDmx 
 * packet for a specific IP Address should not be transmitted at a repeat rate faster 
 * than the maximum repeat rate of a DMX packet containing 512 data slots.
 *
 * Synchronous Data:
 * -----------------
 * In video or media-wall applications, the ability to synchronise multiple universes of 
 * ArtDmx is beneficial. This can be achieved with the ArtSync packet.
 *
 * Data Merging:
 * -------------
 * The Art-Net protocol allows multiple nodes or controllers to transmit ArtDmx data to the 
 * same universe.
 * A node can detect this situation by comparing the IP addresses of received ArtDmx 
 * packets. If ArtDmx packets addressed to the same Universe are received from different IP
 * addresses, a potential conflict exists.
 *
 * The Node can legitimately handle this situation using one of two methods:
 *   - Consider this to be an error condition and await user intervention.
 *   - Automatically merge the data.
 *
 * Nodes should document the approach that is implemented in the product user guide.
 * The Merge option is preferred as it provides a higher level of functionality.
 * Merge is implemented in either LTP or HTP mode as specified by the ArtAddress packet.
 *
 * Merge mode is implemented as follows:
 *
 *   - If ArtDmx is received from differing IP addresses, the data is merged to the DMX 
 *     output. In this situation, ArtPollReply-GoodOutput-Bit3 is set. 
 *     If Art-Poll-TalkToMe Bit 1 is set, an ArtPollReply should be transmitted when merging 
 *     commences.
 *
 * Exit from Merge mode is handled as follows:
 *
 * If ArtAddress AcCancelMerge is received, the Next ArtDmx message received ends Merge mode. 
 * The Node then discards any ArtDmx packets received from an IP address that does not match 
 * the IP address of the ArtDmx packet that terminated Merge mode.
 * If either (but not both) sources of ArtDmx stop, the failed source is held in the 
 * merge buffer for 10 seconds. If, during the 10 second timeout, the failed source 
 * returns, Merge mode continues. If the failed source does not recover, at the end 
 * of the timeout period, the Node exits Merge mode.
 * If both sources of ArtDmx fail, the output holds the last merge result.
 * Merging is limited to two sources, any additional sources will be ignored by the Node.
 * The Merge implementation allows for the following two key modes of operation.
 *
 *   - Combined Control: Two Controllers (Consoles) can operate on a network and 
 *     merge data to multiple Nodes.
 *   - Backup: One Controller (Console) can monitor the network for a failure of the 
 *     primary Controller. If a failure occurs, it can use the ArtAddress AcCancelMerge 
 *     command to take instant control of the network. 
 *
 * When a node provides multiple DMX512 inputs, it is the responsibility of the Node to 
 * handle merging of data. This is because the Node will have only one IP address. If this 
 * were not handled at the Node, ArtDmx packets with identical IP addresses and identical 
 * universe numbers, but conflicting level data would be transmitted to the network.
 * 
 */
static void artnetHandleDmx(artnet_packet_u *artnet)
{
  uint8_t i = 0, j = 0;

  // What port are we working on ?
  for(i = 0; i < ARTNET_GROUPS; i++)
  {
    artnet_group_t *grp = &gArtStatus.cfg->groups[i];
    if(grp == NULL) return;

    if(grp->net != (artnet->dmx.net & 0x7f)) continue;
    if(grp->subnet != (artnet->dmx.sub_uni >> 4)) continue;
    
    for(j = 0; j < grp->ports; j++)
    {
      // Is this an output ?
      if(!(grp->portType[j] & (ARTNET_TYPE_OUTPUT & ARTNET_TYPE_DMX512)))
        continue;

      // Is it for us ?
      if(grp->swout[j] != (artnet->dmx.sub_uni & 0x0f))
        continue;

      // Should we handle seq field ? and ignore 'past' packets ??

      systime_t curr = chVTGetSystemTimeX();
      ipv4_t *ipv4 = (ipv4_t*)(gArtStatus.cfg->iface->buffer + sizeof(eth_frame_t));
      
      // From who is this ?
      if(gArtStatus.lastIpSrc != 0)
      {
        if((TIME_I2MS(gArtStatus.lastDmxPacket) + 2000) < curr) 
          if(ipv4->srcIp != gArtStatus.lastIpSrc) return;
      }

      gArtStatus.lastIpSrc = ipv4->srcIp;
      gArtStatus.lastDmxPacket = curr;

      if(grp->dmxcb != NULL)
        grp->dmxcb(i + j, ntohs(artnet->dmx.length), artnet->dmx.data);
    }
  }
}

/**
 * ArtTodData
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No Action.
 *                  | Unicast Transmit      | Not allowed
 *                  | Broadcast             | Not allowed
 * --------------------------------------------------------------------------------------
 * Node output      | Receive               | No Action.
 * gateway          | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Output Gateway always Directed Broadcasts this packet.
 * --------------------------------------------------------------------------------------
 * Node input       | Receive               | No Action.
 * gateway          | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | No Action.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 *
 */
/*
static void artnetSendTodData(void)
{
}
*/

/**
 * ArtTodRequest
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No Action.
 *                  | Unicast Transmit      | No allowed
 *                  | Broadcast             | Controller directed broadcast to all nodes.
 * --------------------------------------------------------------------------------------
 * Node output      | Receive               | Reply with ArtTodData
 * gateway          | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 * Node input       | Receive               | No Action.
 * gateway          | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Input Gateway Directed Broadcasts to all nodes.
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | No Action.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 *
 * This packet is used to request the Table of RDM Devices (TOD). A Node receiving this 
 * packet must not interpret it as forcing full discovery. Full discovery is only initiated at 
 * power on or when an ArtTodControl.AtcFlush is received.
 *
 * The response is ArtTodData.
 *
 */
static void artnetHandleToDRequest(artnet_packet_u *artnet)
{
  (void)artnet;
  //RDMToD *tod = dmxGetRdmToD(1);
}

/**
 * ArtTodControl
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No Action.
 *                  | Unicast Transmit      | Allowed
 *                  | Broadcast             | Controller directed broadcast to all nodes.
 * --------------------------------------------------------------------------------------
 * Node output      | Receive               | Reply with ArtTodData
 * gateway          | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 * Node input       | Receive               | No Action.
 * gateway          | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Input Gateway Directed Broadcasts to all nodes.
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | No Action.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 *
 * The ArtTodControl packet is used to send RDM control parameters over Art-Net. 
 * The response is ArtTodData.
 *
 */
static void artnetHandleToDControl(artnet_packet_u *artnet)
{
  (void)artnet;
}

/**
 * ArtRdm
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No Action.
 *                  | Unicast Transmit      | Allowed - Preferred
 *                  | Broadcast             | Allowed
 * --------------------------------------------------------------------------------------
 * Node output      | Receive               | No Action.
 * gateway          | Unicast Transmit      | Allowed - Preferred
 *                  | Broadcast             | Allowed
 * --------------------------------------------------------------------------------------
 * Node input       | Receive               | No Action.
 * gateway          | Unicast Transmit      | Allowed - Preferred
 *                  | Broadcast             | Allowed
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | No Action.
 *                  | Unicast Transmit      | Allowed - Preferred
 *                  | Broadcast             | Allowed
 * --------------------------------------------------------------------------------------
 *
 * The ArtRdm packet is used to transport all non-discovery RDM messages over Art-Net.
 *
 */
static void artnetHandleRdm(artnet_packet_u *artnet)
{
  (void)artnet;
}

/**
 * ArtRdmSub
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No Action.
 *                  | Unicast Transmit      | yes
 *                  | Broadcast             | Not Allowed
 * --------------------------------------------------------------------------------------
 * Node output      | Receive               | No Action.
 * gateway          | Unicast Transmit      | Yes
 *                  | Broadcast             | Not Allowed
 * --------------------------------------------------------------------------------------
 * Node input       | Receive               | No Action.
 * gateway          | Unicast Transmit      | Yes
 *                  | Broadcast             | Not Allowed
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | No Action.
 *                  | Unicast Transmit      | Not Allowed
 *                  | Broadcast             | Not Allowed
 * --------------------------------------------------------------------------------------
 *
 * The ArtRdmSub packet is used to transfer Get, Set, GetResponse and SetResponse data 
 * to and from multiple sub-devices within an RDM device. This packet is primarily used by 
 * Art-Net devices that proxy or emulate RDM. It offers very significant bandwidth gains 
 * over the approach of sending multiple ArtRdm packets.
 *
 * Please note that this packet was added at the release of Art-Net II. For backwards 
 * compatibility it is only acceptable to implement this packet in addition to ArtRdm. It 
 * must not be used instead of ArtRdm.
 *
 */
static void artnetHandleRdmSub(artnet_packet_u *artnet)
{
  (void)artnet;
}

/**
 * ArtSync
 *
 * Packet strategy.
 * 
 * Entity           | Direction             | Action
 * --------------------------------------------------------------------------------------
 * Controller       | Receive               | No action.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Directed Broadcast    | Controller broadcasts this packet
 *                                          | to synchronously transfer previous 
 *                                          | ArtDmx packets to Node’s output.
 * --------------------------------------------------------------------------------------
 * Node             | Receive               | Transfer previous ArtDmx packets to output.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 * Media Server     | Receive               | Transfer previous ArtDmx packets to output.
 *                  | Unicast Transmit      | Not allowed.
 *                  | Broadcast             | Not allowed.
 * --------------------------------------------------------------------------------------
 *
 * The Art Sync packet can be used to force nodes to 
 * synchronously output ArtDmx packets to their outputs. 
 * This is useful in video and media-wall applications.
 * A controller that wishes to implement synchronous 
 * transmission will unicast multiple universes of ArtDmx 
 * and then broadcast an ArtSync to synchronously transfer 
 * all the ArtDmx packets to the nodes’ outputs at the same time.
 *
 * Managing Synchronous and non-Synchronous modes
 * 
 * At power on or reset a node shalloperate in non-synchronous mode. 
 * This means that ArtDmx packets will be immediately processed and output.
 * When a node receives an ArtSync packet it should 
 * transfer to synchronous operation. 
 * This means that received ArtDmx packets will be buffered and output 
 * when the next ArtSync is received.
 * In order to allow transition between synchronous and non-synchronous 
 * modes, a node shall time out to non-synchronous operation if an ArtSync 
 * is not received for 4 seconds or more.
 *
 * Multiple controllers
 *
 * In order to allow for multiple controllers on a network, a node shall 
 * compare the source IP of the ArtSync to the source IP of the most recent
 * ArtDmx packet. The ArtSync shall be ignored if the IP addresses do not match.
 * When a port is merging multiple streams of ArtDmx from different IP addresses, 
 * ArtSync packets shall be ignored.
 */
static void artnetHandleSync(artnet_packet_u *artnet)
{
  (void)artnet;
}

/*******************************************/
/* PUBLIC FUNCTIONS                        */
/*******************************************/

void artnetSendFirstPollReply(ustack_iface_t *iface)
{
  artnet_packet_u *artnet = (artnet_packet_u*)(iface->buffer + sizeof(eth_frame_t) + sizeof(ipv4_t) + sizeof(udp_t));
  artnetSendPollReply(artnet);
}

/**
 * TODO
 *
 *
 */
void artnetInit(artnet_config_t *cfg)
{
  if(cfg == NULL)
    return;

  gArtStatus.cfg = cfg;

  dbg("\r\n:: Starting ArtNet and sACN listening");

  if(cfg->ledGreen.port != 0 && cfg->ledGreen.pad != 0)
    palSetPadMode(cfg->ledGreen.port, cfg->ledGreen.pad, PAL_MODE_OUTPUT_PUSHPULL);

  if(cfg->ledRed.port != 0 && cfg->ledRed.pad != 0)
    palSetPadMode(cfg->ledRed.port, cfg->ledRed.pad, PAL_MODE_OUTPUT_PUSHPULL);

  gArtStatus.pollCount = 0;
  gArtStatus.lastDmxPacket = 0;
  gArtStatus.lastIpSrc = 0;
  artnetSetLedsNormal();

  gArtStatus.reportCode = ARTNET_RCPOWEROK;

  // Artnet
  dbgf(":: ARTNET :: Binding Artnet to Port: %d\r\n", gArtStatus.cfg->port);
  ustackUdpAddListener(gArtStatus.cfg->port, artnetParser);

  if(gArtStatus.cfg->sacnEnabled)
  {
    // sACN
    dbgf(":: ARTNET :: Binding sACN to Port: %d\r\n", gArtStatus.cfg->sacnPort);
    ustackUdpAddListener(gArtStatus.cfg->sacnPort, sacnParser);
  }

  ustackQueueSendPacket(artnetSendFirstPollReply);
}

/**
 * Parses the artnet opcode, and calls it's respective
 * function.
 *
 * uint16_t length - the packet length
 * uint8_t data    - the packet data
 */
void artnetParser(ustack_iface_t *iface, uint16_t len)
{
  (void)len;
  (void)iface;

  artnet_packet_u *artnet = (artnet_packet_u*)(iface->buffer + sizeof(eth_frame_t) + sizeof(ipv4_t) + sizeof(udp_t));

  uint16_t proto = ((artnet->header.prot_ver_hi << 8) & 0xff) |
      (artnet->header.prot_ver_low & 0xff);
  
  // Anything below protocol version 14 should be ignored
  if(proto < ARTNET_VERSION)
    return;
  
  switch(artnet->header.opCode)
  {
    case ARTNET_OPCODE_POLL:
      artnetSendPollReply(artnet);
      break;
    case ARTNET_OPCODE_SYNC:
      artnetHandleSync(artnet);
      break;
    case ARTNET_OPCODE_IPPROG:
      artnetHandleIPProg(artnet);
      break;
    case ARTNET_OPCODE_ADDRESS:
      artnetHandleAddress(artnet);
      break;
    case ARTNET_OPCODE_DMX:
      artnetHandleDmx(artnet);
      break;
    case ARTNET_OPCODE_TODREQUEST:
      artnetHandleToDRequest(artnet);
      break;
    case ARTNET_OPCODE_TODCONTROL:
      artnetHandleToDControl(artnet);
      break;
    case ARTNET_OPCODE_RDM:
      artnetHandleRdm(artnet);
      break;
    case ARTNET_OPCODE_RDMSUB:
      artnetHandleRdmSub(artnet);
      break;
  };
}

/**
 * TODO
 *
 */
void sacnParser(ustack_iface_t *iface, uint16_t len)
{
  (void)iface;
  (void)len;
  
  // Universe is based on IP
  //uint16_t puni = HTONS(UDPBUF->destipaddr[1]);
  //dbgf("Got sACN Packet for Universe %d\r\n", puni);
}


