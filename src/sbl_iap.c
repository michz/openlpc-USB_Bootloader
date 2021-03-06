//-----------------------------------------------------------------------------
// Software that is described herein is for illustrative purposes only
// which provides customers with programming information regarding the
// products. This software is supplied "AS IS" without any warranties.
// NXP Semiconductors assumes no responsibility or liability for the
// use of the software, conveys no license or title under any patent,
// copyright, or mask work right to the product. NXP Semiconductors
// reserves the right to make changes in the software without
// notification. NXP Semiconductors also make no representation or
// warranty that such application will be suitable for the specified
// use without further testing or modification.
//-----------------------------------------------------------------------------

/***********************************************************************
 * Code Red Technologies - Minor modifications to original NXP AN10866
 * example code for use in RDB1768 secondary USB bootloader based on
 * LPCUSB USB stack.
 * *********************************************************************/

#include "type.h"
#include "sbl_iap.h"
#include "sbl_config.h"
#include "LPC17xx.h"
#include "log.h"

const unsigned sector_start_map[MAX_FLASH_SECTOR] = {SECTOR_0_START,             \
SECTOR_1_START,SECTOR_2_START,SECTOR_3_START,SECTOR_4_START,SECTOR_5_START,      \
SECTOR_6_START,SECTOR_7_START,SECTOR_8_START,SECTOR_9_START,SECTOR_10_START,     \
SECTOR_11_START,SECTOR_12_START,SECTOR_13_START,SECTOR_14_START,SECTOR_15_START, \
SECTOR_16_START,SECTOR_17_START,SECTOR_18_START,SECTOR_19_START,SECTOR_20_START, \
SECTOR_21_START,SECTOR_22_START,SECTOR_23_START,SECTOR_24_START,SECTOR_25_START, \
SECTOR_26_START,SECTOR_27_START,SECTOR_28_START,SECTOR_29_START};

const unsigned sector_end_map[MAX_FLASH_SECTOR] = {SECTOR_0_END,SECTOR_1_END,    \
SECTOR_2_END,SECTOR_3_END,SECTOR_4_END,SECTOR_5_END,SECTOR_6_END,SECTOR_7_END,   \
SECTOR_8_END,SECTOR_9_END,SECTOR_10_END,SECTOR_11_END,SECTOR_12_END,             \
SECTOR_13_END,SECTOR_14_END,SECTOR_15_END,SECTOR_16_END,SECTOR_17_END,           \
SECTOR_18_END,SECTOR_19_END,SECTOR_20_END,SECTOR_21_END,SECTOR_22_END,           \
SECTOR_23_END,SECTOR_24_END,SECTOR_25_END,SECTOR_26_END,                         \
SECTOR_27_END,SECTOR_28_END,SECTOR_29_END                                         };

unsigned param_table[5];
unsigned result_table[5];

char flash_buf[FLASH_BUF_SIZE];

unsigned *flash_address = 0;
unsigned byte_ctr = 0;

unsigned sector_erased_map[MAX_FLASH_SECTOR];

void write_data(unsigned cclk,unsigned flash_address,unsigned * flash_data_buf, unsigned count);
void find_erase_prepare_sector(unsigned cclk, unsigned flash_address);
void erase_sector(unsigned start_sector,unsigned end_sector,unsigned cclk);
void prepare_sector(unsigned start_sector,unsigned end_sector,unsigned cclk);
void iap_entry(unsigned param_tab[],unsigned result_tab[]);

void reset_sector_erasure_status() {
    debug("Clearing erasure status of all sectors to allow rewriting");
    for(int i = 0; i < MAX_FLASH_SECTOR; i++) {
        sector_erased_map[i] = false;
    }
}

unsigned write_flash(unsigned * dst, char * src, unsigned no_of_bytes)
{
    if (flash_address == 0) {
      /* Store flash start address */
      flash_address = (unsigned *)dst;
    }

    for(unsigned int i = 0;i<no_of_bytes;i++ ) {
      flash_buf[(byte_ctr+i)] = *(src+i);
    }
    byte_ctr = byte_ctr + no_of_bytes;

    if(byte_ctr == FLASH_BUF_SIZE) {

      debug("writing to user flash address 0x%x", flash_address);
      /* We have accumulated enough bytes to trigger a flash write */
      find_erase_prepare_sector(SystemCoreClock/1000, (unsigned)flash_address);
      if(result_table[0] != CMD_SUCCESS)
      {
        debug("Couldn't prepare flash sector - can't recover");
        while(1); /* No way to recover. Just let Windows report a write failure */
      }
      write_data(SystemCoreClock/1000,(unsigned)flash_address,(unsigned *)flash_buf,FLASH_BUF_SIZE);
      if(result_table[0] != CMD_SUCCESS)
      {
        debug("Flash write failed (code %d) - can't recover", result_table[0]);
        while(1); /* No way to recover. Just let Windows report a write failure */
      }


      /* Reset byte counter and flash address */
      byte_ctr = 0;
      flash_address = 0;
    } else {
      debug("buffered data to write until we have a full sector's worth");
    }
    return(CMD_SUCCESS);
}

void find_erase_prepare_sector(unsigned cclk, unsigned flash_address) {
    __disable_irq();
    for(unsigned int i = USER_START_SECTOR; i <= MAX_USER_SECTOR; i++) {
        if(flash_address < sector_end_map[i]) {
            if(!sector_erased_map[i]) {
                prepare_sector(i, i,cclk);
                erase_sector(i, i, cclk);
                debug("Prepared and erased sector %d", i);
                sector_erased_map[i] = true;
            }
            prepare_sector(i, i, cclk);
            break;
        }
    }
    __enable_irq();
}

void write_data(unsigned cclk,unsigned flash_address,unsigned * flash_data_buf, unsigned count)
{
    __disable_irq();
    param_table[0] = COPY_RAM_TO_FLASH;
    param_table[1] = flash_address;
    param_table[2] = (unsigned)flash_data_buf;
    param_table[3] = count;
    param_table[4] = cclk;
    iap_entry(param_table,result_table);
    __enable_irq();

}

void erase_sector(unsigned start_sector,unsigned end_sector,unsigned cclk)
{
    param_table[0] = ERASE_SECTOR;
    param_table[1] = start_sector;
    param_table[2] = end_sector;
    param_table[3] = cclk;
    iap_entry(param_table,result_table);
}

void prepare_sector(unsigned start_sector,unsigned end_sector,unsigned cclk)
{
    param_table[0] = PREPARE_SECTOR_FOR_WRITE;
    param_table[1] = start_sector;
    param_table[2] = end_sector;
    param_table[3] = cclk;
    iap_entry(param_table,result_table);
}

void iap_entry(unsigned param_tab[],unsigned result_tab[])
{
    void (*iap)(unsigned [],unsigned []);

    iap = (void (*)(unsigned [],unsigned []))IAP_ADDRESS;
    iap(param_tab,result_tab);
}


void execute_user_code(void)
{
    void (*user_code_entry)(void);

    unsigned *p;    // used for loading address of reset handler from user flash

    // Disable interrupts and turn off all peripherals so the user code doesn't
    // accidentally jump back to the old vector table
    __disable_irq();
    LPC_SC->PCONP = 0x001817BE;

    /* Change the Vector Table to the USER_FLASH_START
       in case the user application uses interrupts */
    SCB->VTOR = (USER_FLASH_START & 0x1FFFFF80);

    // The very top of the user flash should contain the interrupt handler
    // vector. The first word should be the initial stack pointer. The second
    // word should contain the address of the Reset_Handler (i.e.
    // USER_FLASH_START + 4).
    p = (unsigned *)(USER_FLASH_START + 4);

    // Set user_code_entry to be the address contained in that second word
    // of user flash.
    user_code_entry = (void *) *p;

    __enable_irq();
    // Jump to user application
    user_code_entry();
}


int user_code_present(void) {
    param_table[0] = BLANK_CHECK_SECTOR;
    param_table[1] = USER_START_SECTOR;
    param_table[2] = USER_START_SECTOR;
    iap_entry(param_table,result_table);
    if( result_table[0] == CMD_SUCCESS )
    {

        return (false);
    }

    return (true);
}

/*
void check_isp_entry_pin(void)
{
    unsigned long i,j;

    for(i=0; i < 60 ; i++)
    {
          if((LPC_GPIO2->FIOPIN & (BOOTLOADER_ENTRY_GPIO_PORT
                  << BOOTLOADER_ENTRY_GPIO_PIN)) == 0)
        {
            break;
        }
        for(j=0;j< (1<<15);j++);
    }
    if( i == 60)
    {
        execute_user_code();
    }
}
*/

void erase_user_flash(void)
{
    debug("Erasing user flash");
    prepare_sector(USER_START_SECTOR, MAX_USER_SECTOR, SystemCoreClock/1000);
    erase_sector(USER_START_SECTOR, MAX_USER_SECTOR, SystemCoreClock/1000);
    if(result_table[0] != CMD_SUCCESS)
    {
      debug("Unable to erase user flash - can't recover");
      while(1); /* No way to recover. Just let Windows report a write failure */
    }
}
