/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * MicroPython-ESP32 YModem driver/Module
 *
 * Copyright (C) 2017 Boris Lovosevic (https://github.com/loboris)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "sdkconfig.h"

#if CONFIG_MICROPY_RX_BUFFER_SIZE > 1079

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/crc.h"
#include "uart.h"
#include "modymodem.h"
#include "py/ringbuf.h"
#include "mphalport.h"
#include "rom/uart.h"

#include <fcntl.h>
#include "extmod/vfs_native.h"

#ifdef CONFIG_MICROPY_USE_TELNET
#include "telnet.h"
#endif

//------------------------------------------------------------------------
static unsigned short crc16(const unsigned char *buf, unsigned long count)
{
  unsigned short crc = 0;
  int i;

  while(count--) {
    crc = crc ^ *buf++ << 8;

    for (i=0; i<8; i++) {
      if (crc & 0x8000) crc = crc << 1 ^ 0x1021;
      else crc = crc << 1;
    }
  }
  return crc;
}

/*
//---------------------------------------------------------------------------
static int32_t receive_Bytes (unsigned char *buf, int size, uint32_t timeout)
{
	unsigned char ch;
    int cb = -1;
    int recv = 0;

    while (recv < size) {
    	cb = mp_hal_stdin_rx_chr(timeout);
    	if (cb < 0) break;
    	buf[recv++] = (uint8_t)cb;
    }
	if (recv == 0) return -1;
	return 0;
}
*/

//--------------------------------------------------------------
static int32_t Receive_Byte (unsigned char *c, uint32_t timeout)
{
	int cb = mp_hal_stdin_rx_chr(timeout);

	if (cb < 0) return -1;
	*c = (uint8_t)cb;
    return 0;
}

//------------------------
static void uart_consume()
{
	xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
	uart0_raw_input = 1;
	xSemaphoreGive(uart0_mutex);
	int cb = mp_hal_stdin_rx_chr(1);
    while (cb >= 0) {
    	cb = mp_hal_stdin_rx_chr(1);
    }
	xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
    uart0_raw_input = 0;
	xSemaphoreGive(uart0_mutex);
}

//----------------------------------------
static void send_Bytes(char *buf, int len)
{
    while (len--) {
        uart_tx_one_char(*buf++);
    }
}
//--------------------------------
static uint32_t Send_Byte (char c)
{
	send_Bytes(&c,1);
	return 0;
}

//----------------------------
static void send_CA ( void ) {
  Send_Byte(CA);
  Send_Byte(CA);
}

//-----------------------------
static void send_ACK ( void ) {
  Send_Byte(ACK);
}

//----------------------------------
static void send_ACKCRC16 ( void ) {
  Send_Byte(ACK);
  Send_Byte(CRC16);
}

//-----------------------------
static void send_NAK ( void ) {
  Send_Byte(NAK);
}

//-------------------------------
static void send_CRC16 ( void ) {
  Send_Byte(CRC16);
}


/**
  * @brief  Receive a packet from sender
  * @param  data
  * @param  timeout
  * @param  length
  *    >0: packet length
  *     0: end of transmission
  *    -1: abort by sender
  *    -2: error or crc error
  * @retval 0: normally return
  *        -1: timeout
  *        -2: abort by user
  */
//--------------------------------------------------------------------------
static int32_t Receive_Packet (uint8_t *data, int *length, uint32_t timeout)
{
  int count, packet_size, i;
  unsigned char ch;
  *length = 0;
  
  // receive 1st byte
  if (Receive_Byte(&ch, timeout) < 0) {
	  return -1;
  }

  switch (ch) {
    case SOH:
		packet_size = PACKET_SIZE;
		break;
    case STX:
		packet_size = PACKET_1K_SIZE;
		break;
    case EOT:
        *length = 0;
        return 0;
    case CA:
    	if (Receive_Byte(&ch, timeout) < 0) {
    		return -2;
    	}
    	if (ch == CA) {
    		*length = -1;
    		return 0;
    	}
    	else return -1;
    case ABORT1:
    case ABORT2:
    	return -2;
    default:
    	vTaskDelay(100 / portTICK_RATE_MS);
    	uart_consume();
    	return -1;
  }

  *data = (uint8_t)ch;
  uint8_t *dptr = data+1;
  count = packet_size + PACKET_OVERHEAD-1;

  for (i=0; i<count; i++) {
	  if (Receive_Byte(&ch, timeout) < 0) {
		  return -1;
	  }
	  *dptr++ = (uint8_t)ch;;
  }

  if (data[PACKET_SEQNO_INDEX] != ((data[PACKET_SEQNO_COMP_INDEX] ^ 0xff) & 0xff)) {
      *length = -2;
      return 0;
  }
  if (crc16(&data[PACKET_HEADER], packet_size + PACKET_TRAILER) != 0) {
      *length = -2;
      return 0;
  }

  *length = packet_size;
  return 0;
}

// Receive a file using the ymodem protocol.
//-------------------------------------------------------------------------------
int Ymodem_Receive (FILE *ffd, unsigned int maxsize, char* getname, char *errmsg)
{
  uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
  uint8_t *file_ptr;
  char file_size[128];
  unsigned int i, file_len, write_len, session_done, file_done, packets_received, errors, size = 0;
  int packet_length = 0;
  file_len = 0;
  int eof_cnt = 0;
  
  for (session_done = 0, errors = 0; ;) {
    for (packets_received = 0, file_done = 0; ;) {
      switch (Receive_Packet(packet_data, &packet_length, NAK_TIMEOUT)) {
        case 0:  // normal return
          switch (packet_length) {
            case -1:
                // Abort by sender
                send_ACK();
                size = -1;
                sprintf(errmsg, "Abort by sender");
                goto exit;
            case -2:
                // error
                errors ++;
                if (errors > 5) {
                  send_CA();
                  size = -2;
                  sprintf(errmsg, "Error limit exceeded");
                  goto exit;
                }
                send_NAK();
                break;
            case 0:
                // End of transmission
            	eof_cnt++;
            	if (eof_cnt == 1) {
            		send_NAK();
            	}
            	else {
            		send_ACKCRC16();
            	}
                break;
            default:
              // ** Normal packet **
              if (eof_cnt > 1) {
          		send_ACK();
              }
              else if ((packet_data[PACKET_SEQNO_INDEX] & 0xff) != (packets_received & 0x000000ff)) {
                errors ++;
                if (errors > 5) {
                  send_CA();
                  size = -3;
                  sprintf(errmsg, "Wrong packet type received");
                  goto exit;
                }
                send_NAK();
              }
              else {
                if (packets_received == 0) {
                  // ** First packet, Filename packet **
                  if (packet_data[PACKET_HEADER] != 0) {
                    errors = 0;
                    // ** Filename packet has valid data
                    if (getname) {
                      for (i = 0, file_ptr = packet_data + PACKET_HEADER; ((*file_ptr != 0) && (i < 64));) {
                        *getname = *file_ptr++;
                        getname++;
                      }
                      *getname = '\0';
                    }
                    for (i = 0, file_ptr = packet_data + PACKET_HEADER; (*file_ptr != 0) && (i < packet_length);) {
                      file_ptr++;
                    }
                    for (i = 0, file_ptr ++; (*file_ptr != ' ') && (i < FILE_SIZE_LENGTH);) {
                      file_size[i++] = *file_ptr++;
                    }
                    file_size[i++] = '\0';
                    if (strlen(file_size) > 0) size = strtol(file_size, NULL, 10);
                    else size = 0;

                    // Test the size of the file
                    if ((size < 1) || (size > maxsize)) {
                      // End session
                      send_CA();
                      if (size > maxsize) size = -9;
                      else size = -4;
                      sprintf(errmsg, "Wrong file size");
                      goto exit;
                    }

                    file_len = 0;
                    send_ACKCRC16();
                  }
                  // Filename packet is empty, end session
                  else {
                      errors ++;
                      if (errors > 5) {
                        send_CA();
                        sprintf(errmsg, "Filename packet is empty, end session");
                        size = -5;
                        goto exit;
                      }
                      send_NAK();
                  }
                }
                else {
                  // ** Data packet **
                  // Write received data to file
                  if (file_len < size) {
                    file_len += packet_length;  // total bytes received
                    if (file_len > size) {
                    	write_len = packet_length - (file_len - size);
                    	file_len = size;
                    }
                    else write_len = packet_length;

                    int written_bytes = fwrite((char*)(packet_data + PACKET_HEADER), 1, write_len, ffd);
                    if (written_bytes != write_len) { //failed
                      /* End session */
                      send_CA();
                      size = -6;
                      sprintf(errmsg, "fwrite() error [%d <> %d]", written_bytes, write_len);
                      goto exit;
                    }
                  }
                  //success
                  errors = 0;
                  send_ACK();
                }
                packets_received++;
              }
          }
          break;
        case -2:  // user abort
          send_CA();
          size = -7;
          sprintf(errmsg, "User abort");
          goto exit;
        default: // timeout
          if (eof_cnt > 1) {
        	file_done = 1;
          }
          else {
			  errors ++;
			  if (errors > MAX_ERRORS) {
				send_CA();
				size = -8;
                sprintf(errmsg, "Max errors");
				goto exit;
			  }
			  send_CRC16();
          }
      }
      if (file_done != 0) {
    	  session_done = 1;
    	  break;
      }
    }
    if (session_done != 0) break;
  }

exit:
  return size;
}

//------------------------------------------------------------------------------------
static void Ymodem_PrepareIntialPacket(uint8_t *data, char *fileName, uint32_t length)
{
  uint16_t tempCRC;

  memset(data, 0, PACKET_SIZE + PACKET_HEADER);
  // Make first three packet
  data[0] = SOH;
  data[1] = 0x00;
  data[2] = 0xff;
  
  // add filename
  sprintf((char *)(data+PACKET_HEADER), "%s", fileName);

  //add file site
  sprintf((char *)(data + PACKET_HEADER + strlen((char *)(data+PACKET_HEADER)) + 1), "%d", length);
  data[PACKET_HEADER + strlen((char *)(data+PACKET_HEADER)) +
	   1 + strlen((char *)(data + PACKET_HEADER + strlen((char *)(data+PACKET_HEADER)) + 1))] = ' ';
  
  // add crc
  tempCRC = crc16(&data[PACKET_HEADER], PACKET_SIZE);
  data[PACKET_SIZE + PACKET_HEADER] = tempCRC >> 8;
  data[PACKET_SIZE + PACKET_HEADER + 1] = tempCRC & 0xFF;
}

//-------------------------------------------------
static void Ymodem_PrepareLastPacket(uint8_t *data)
{
  uint16_t tempCRC;
  
  memset(data, 0, PACKET_SIZE + PACKET_HEADER);
  data[0] = SOH;
  data[1] = 0x00;
  data[2] = 0xff;
  tempCRC = crc16(&data[PACKET_HEADER], PACKET_SIZE);
  //tempCRC = crc16_le(0, &data[PACKET_HEADER], PACKET_SIZE);
  data[PACKET_SIZE + PACKET_HEADER] = tempCRC >> 8;
  data[PACKET_SIZE + PACKET_HEADER + 1] = tempCRC & 0xFF;
}

//-----------------------------------------------------------------------------------------
static void Ymodem_PreparePacket(uint8_t *data, uint8_t pktNo, uint32_t sizeBlk, FILE *ffd)
{
  uint16_t i, size;
  uint16_t tempCRC;
  
  data[0] = STX;
  data[1] = (pktNo & 0x000000ff);
  data[2] = (~(pktNo & 0x000000ff));

  size = sizeBlk < PACKET_1K_SIZE ? sizeBlk :PACKET_1K_SIZE;
  // Read block from file
  if (size > 0) {
	  size = fread(data + PACKET_HEADER, 1, size, ffd);
  }

  if ( size  < PACKET_1K_SIZE) {
    for (i = size + PACKET_HEADER; i < PACKET_1K_SIZE + PACKET_HEADER; i++) {
      data[i] = 0x00; // EOF (0x1A) or 0x00
    }
  }
  tempCRC = crc16(&data[PACKET_HEADER], PACKET_1K_SIZE);
  //tempCRC = crc16_le(0, &data[PACKET_HEADER], PACKET_1K_SIZE);
  data[PACKET_1K_SIZE + PACKET_HEADER] = tempCRC >> 8;
  data[PACKET_1K_SIZE + PACKET_HEADER + 1] = tempCRC & 0xFF;
}

//-------------------------------------------------------------
static uint8_t Ymodem_WaitResponse(uint8_t ackchr, uint8_t tmo)
{
  unsigned char receivedC;
  uint32_t errors = 0;

  do {
    if (Receive_Byte(&receivedC, NAK_TIMEOUT) == 0) {
      if (receivedC == ackchr) {
        return 1;
      }
      else if (receivedC == CA) {
        send_CA();
        return 2; // CA received, Sender abort
      }
      else if (receivedC == NAK) {
        return 3;
      }
      else {
        return 4;
      }
    }
    else {
      errors++;
    }
  }while (errors < tmo);
  return 0;
}


//---------------------------------------------------------------------------------------
int Ymodem_Transmit (char* sendFileName, unsigned int sizeFile, FILE *ffd, char *err_msg)
{
  uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
  uint16_t blkNumber;
  unsigned char receivedC;
  int err;
  uint32_t size = 0;

  // Wait for response from receiver
  err = 0;
  do {
    Send_Byte(CRC16);
  } while (Receive_Byte(&receivedC, NAK_TIMEOUT) < 0 && err++ < 45);

  if (err >= 45 || receivedC != CRC16) {
    send_CA();
    sprintf(err_msg, "No response from host");
    return -1;
  }
  
  // === Prepare first block and send it =======================================
  /* When the receiving program receives this block and successfully
   * opened the output file, it shall acknowledge this block with an ACK
   * character and then proceed with a normal YMODEM file transfer
   * beginning with a "C" or NAK tranmsitted by the receiver.
   */
  Ymodem_PrepareIntialPacket(packet_data, sendFileName, sizeFile);
  do 
  {
    // Send Packet
	  send_Bytes((char *)packet_data, PACKET_SIZE + PACKET_OVERHEAD);

	// Wait for Ack
    err = Ymodem_WaitResponse(ACK, 10);
    if (err == 0 || err == 4) {
      send_CA();
      sprintf(err_msg, "No ACK from host");
      return -2;                  // timeout or wrong response
    }
    else if (err == 2) {
        sprintf(err_msg, "Host abort");
    	return 98; // abort
    }
  }while (err != 1);

  // After initial block the receiver sends 'C' after ACK
  if (Ymodem_WaitResponse(CRC16, 10) != 1) {
    send_CA();
    sprintf(err_msg, "No CRC after ACK");
    return -3;
  }
  
  // === Send file blocks ======================================================
  size = sizeFile;
  blkNumber = 0x01;
  
  // Resend packet if NAK  for a count of 10 else end of communication
  while (size)
  {
    // Prepare and send next packet
    Ymodem_PreparePacket(packet_data, blkNumber, size, ffd);
    do
    {
    	send_Bytes((char *)packet_data, PACKET_1K_SIZE + PACKET_OVERHEAD);

      // Wait for Ack
      err = Ymodem_WaitResponse(ACK, 10);
      if (err == 1) {
        blkNumber++;
        if (size > PACKET_1K_SIZE) size -= PACKET_1K_SIZE; // Next packet
        else size = 0; // Last packet sent
      }
      else if (err == 0 || err == 4) {
        send_CA();
        sprintf(err_msg, "Timeout or wrong response");
        return -4;                  // timeout or wrong response
      }
      else if (err == 2) {
          sprintf(err_msg, "Host abort");
    	  return -5; // abort
      }
    }while(err != 1);
  }
  
  // === Send EOT ==============================================================
  Send_Byte(EOT); // Send (EOT)
  // Wait for Ack
  do 
  {
    // Wait for Ack
    err = Ymodem_WaitResponse(ACK, 10);
    if (err == 3) {   // NAK
      Send_Byte(EOT); // Send (EOT)
    }
    else if (err == 0 || err == 4) {
      send_CA();
      sprintf(err_msg, "Timeout or wrong response on EOF");
      return -6;                  // timeout or wrong response
    }
    else if (err == 2) {
        sprintf(err_msg, "Host abort on EOT");
    	return -7; // abort
    }
  }while (err != 1);
  
  // === Receiver requests next file, prepare and send last packet =============
  if (Ymodem_WaitResponse(CRC16, 10) != 1) {
	sprintf(err_msg, "No CRC after EOF");
    send_CA();
    return -8;
  }

  Ymodem_PrepareLastPacket(packet_data);
  do 
  {
	// Send Packet
	  send_Bytes((char *)packet_data, PACKET_SIZE + PACKET_OVERHEAD);

	// Wait for Ack
    err = Ymodem_WaitResponse(ACK, 10);
    if (err == 0 || err == 4) {
      send_CA();
      sprintf(err_msg, "Timeout or wrong response on last packet");
      return -9;                  // timeout or wrong response
    }
    else if (err == 2) {
        sprintf(err_msg, "Host abort on last packet");
    	return -10; // abort
    }
  }while (err != 1);
  
  return 0; // file transmitted successfully
}

#endif

// ===== Module methods ===============================================================================

//--------------------------------------------
STATIC mp_obj_t ymodem_recv(mp_obj_t fname_in)
{
#ifdef CONFIG_MICROPY_USE_TELNET
	if (telnet_loggedin()) {
		mp_printf(&mp_plat_print, "Cannot execute from Telnet session\n");
		return mp_const_none;
	}
#endif

#if CONFIG_MICROPY_RX_BUFFER_SIZE > 1079
	if (CONFIG_MICROPY_RX_BUFFER_SIZE < 1080) {
		mp_printf(&mp_plat_print, "Minimum stdio RX buffer size is 1080 bytes, please rebuild.\n");
		return mp_const_none;
	}

	const char *fname = mp_obj_str_get_str(fname_in);
    char fullname[128] = {'\0'};
    int err = 1;
    char err_msg[128] = {'\0'};
    char orig_name[128] = {'\0'};

    if (physicalPath(fname, fullname) != 0) {
    	sprintf(err_msg, "File name cannot be resolved");
		goto exit;
    }

	// Open the file
	FILE *ffd = fopen(fullname, "wb");
	if (ffd) {
		mp_printf(&mp_plat_print, "\nReceiving file, please start YModem transfer on host ...\n");
		mp_printf(&mp_plat_print, "(Press \"a\" to abort)\n");

		xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
		uart0_raw_input = 1;
		xSemaphoreGive(uart0_mutex);

		int rec_res = Ymodem_Receive(ffd, 1000000, orig_name, err_msg);

		xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
		uart0_raw_input = 0;
		xSemaphoreGive(uart0_mutex);

		fclose(ffd);
		mp_printf(&mp_plat_print, "\r\n");

		if (rec_res > 0) {
			err = 0;
			mp_printf(&mp_plat_print, "File received, size=%d, original name: \"%s\"\n", rec_res, orig_name);
		}
		else remove(fullname);
	}
	else {
		sprintf(err_msg, "Opening file \"%s\" for writing.", fname);
	}

exit:
	mp_printf(&mp_plat_print, "\n%s%s\n", ((err == 0) ? "" : "Error: "), err_msg);
	return mp_const_none;
#else
	mp_printf(&mp_plat_print, "Minimum stdin RX buffer size is 1080 bytes, please rebuild.\n");
	return mp_const_none;
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ymodem_recv_obj, ymodem_recv);

//--------------------------------------------
STATIC mp_obj_t ymodem_send(mp_obj_t fname_in)
{
#ifdef CONFIG_MICROPY_USE_TELNET
	if (telnet_loggedin()) {
		mp_printf(&mp_plat_print, "Cannot execute from Telnet session\n");
		return mp_const_none;
	}
#endif

#if CONFIG_MICROPY_RX_BUFFER_SIZE > 1079
    const char *fname = mp_obj_str_get_str(fname_in);
    int fsize = 0, err = 0;
    char fullname[128] = {'\0'};
    char err_msg[128] = {'\0'};

    if (physicalPath(fname, fullname) != 0) {
    	sprintf(err_msg, "File name cannot be resolved");
		goto exit;
    }

    // Get file size
	struct stat buf;
	int res = stat(fullname, &buf);
	if (res < 0) {
		sprintf(err_msg, "Get file size.");
		goto exit;
	}
	fsize = buf.st_size;

	// Open the file
	FILE *ffd = fopen(fullname, "rb");
	if (ffd) {
		mp_printf(&mp_plat_print, "\nSending file, please start YModem receive on host ...\n");
		mp_printf(&mp_plat_print, "(Press \"a\" to abort)\n");

		xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
		uart0_raw_input = 1;
		xSemaphoreGive(uart0_mutex);

		int trans_res = Ymodem_Transmit((char *)fname, fsize, ffd, err_msg);

		xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
	    uart0_raw_input = 0;
		xSemaphoreGive(uart0_mutex);

		fclose(ffd);
		mp_printf(&mp_plat_print, "\r\n");
		if (trans_res == 0) {
			err = 0;
			sprintf(err_msg, "Transfer complete, %d bytes sent", fsize);
		}
	}
	else sprintf(err_msg, "Opening file \"%s\" for reading.", fname);

exit:
mp_printf(&mp_plat_print, "\n%s%s\n", ((err == 0) ? "" : "Error: "), err_msg);
	return mp_const_none;
#else
	mp_printf(&mp_plat_print, "Minimum stdin RX buffer size is 1080 bytes, please rebuild.\n");
	return mp_const_none;
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ymodem_send_obj, ymodem_send);



//--------------------------------------------------------------
STATIC const mp_rom_map_elem_t ymodem_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ymodem) },

    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&ymodem_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&ymodem_recv_obj) }
};

STATIC MP_DEFINE_CONST_DICT(ymodem_module_globals, ymodem_module_globals_table);

//----------------------------------------
const mp_obj_module_t mp_module_ymodem = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&ymodem_module_globals,
};

