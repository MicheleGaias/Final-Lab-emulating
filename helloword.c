#include <stdio.h>
#include "xstatus.h"
#include "platform.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xuartlite_l.h"
#include "xparameters.h"
#include "xtmrctr_l.h"

// --- CONFIGURAZIONE INDIRIZZI ---
#ifndef SDT
#define TMRCTR_BASEADDR    XPAR_TMRCTR_0_BASEADDR
#else
#define TMRCTR_BASEADDR    XPAR_XTMRCTR_0_BASEADDR
#endif

#define UART_MODE (u8)1
#define UART_BASEADDR XPAR_UARTLITE_0_BASEADDR
#define TIMER_COUNTER_0   0

// LED FRECCE (Scheda)
volatile int * gpio_leds_data = (volatile int*) 0x40000000;
volatile int * gpio_leds_tri  = (volatile int*) 0x40000004;

// LED MOTORI (Breadboard)
volatile int * gpio_motors_data1 = (volatile int*) 0x40020000;
volatile int * gpio_motors_tri1  = (volatile int*) 0x40020004;

// INTERRUPT
volatile int * IER = (volatile int*)  0x41200008;
volatile int * MER = (volatile int*)  0x4120001C;
volatile int * IISR = (volatile int*)  0x41200000;
volatile int * IIAR = (volatile int*)  0x4120000C;

// --- VARIABILI GLOBALI ---
volatile u8 vel_left = 0;
volatile u8 vel_right = 0;
volatile int pwm_count = 0;

volatile int current_direction_mask = 0;

// --- DICHIARAZIONI ---
void myISR(void) __attribute__((interrupt_handler));
void ProcessCommand(u32 data);
u32 my_XUartLite_RecvByte(UINTPTR BaseAddress);
int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber);

int main()
{
  int Status;
  init_platform();

  *gpio_motors_tri1 = 0x00000000;
  *gpio_leds_tri    = 0x00000000;

  *gpio_motors_data1 = 0;
  *gpio_leds_data = 0;

  u32 uart_val;

  Status = TmrCtrLowLevelExample(TMRCTR_BASEADDR, TIMER_COUNTER_0);
  if (Status != XST_SUCCESS) {
     xil_printf("Timer Failed\r\n");
     return XST_FAILURE;
  }

  *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
  *MER = 0x3;
  microblaze_enable_interrupts();

  xil_printf("--- SISTEMA PRONTO ---\r\n");
  xil_printf("Comandi: t=Dritto, r/e/w=Sinistra, y/u/i=Destra\r\n");

  ProcessCommand(' ');

  while(1){
       uart_val = my_XUartLite_RecvByte(UART_BASEADDR);
       if (uart_val != 200) {
         ProcessCommand(uart_val);
       }
  }
  cleanup_platform();
}

u32 my_XUartLite_RecvByte(UINTPTR BaseAddress)
{
  if(XUartLite_IsReceiveEmpty(BaseAddress)){
     return (u8) 200;
  }
  u8 data = (u8)XUartLite_ReadReg(BaseAddress,XUL_RX_FIFO_OFFSET);

  if (data == '\r' || data == '\n') return 200;
  return (u32)data;
}

// LOGICA AGGIORNATA CON SPEGNIMENTO DIREZIONE
void ProcessCommand(u32 data){
    int led_smd_val = 0;

    switch (data){
        // --- DRITTO ---
        case 't':
            xil_printf("CMD: Dritto\r\n");
            current_direction_mask = 0x5; // Entrambi DIR Accesi
            vel_left = 150; vel_right = 150;
            led_smd_val = 0x3;
            break;

        // --- SINISTRA (Left) ---
        case 'r': // Curve Wide
            xil_printf("CMD: Curva SX Larga\r\n");
            current_direction_mask = 0x5; // Entrambi DIR Accesi
            vel_left = 100; vel_right = 150;
            led_smd_val = 0x1;
            break;
        case 'e': // Sharp Left
            xil_printf("CMD: Curva SX Stretta\r\n");
            current_direction_mask = 0x5; // Entrambi DIR Accesi
            vel_left = 50; vel_right = 150;
            led_smd_val = 0x1;
            break;
        case 'w': // Pivot Left (MOTORE SX FERMO -> SPENGO DIR SX)
            xil_printf("CMD: Pivot SX (Ruota ferma)\r\n");
            // Bit 0 (Dir B - DX) = 1, Bit 2 (Dir A - SX) = 0
            current_direction_mask = 0x1; // <--- MODIFICA QUI
            vel_left = 0; vel_right = 150;
            led_smd_val = 0x1;
            break;

        // --- DESTRA (Right) ---
        case 'y': // Curve Wide
            xil_printf("CMD: Curva DX Larga\r\n");
            current_direction_mask = 0x5; // Entrambi DIR Accesi
            vel_left = 150; vel_right = 100;
            led_smd_val = 0x2;
            break;
        case 'u': // Sharp Right
            xil_printf("CMD: Curva DX Stretta\r\n");
            current_direction_mask = 0x5; // Entrambi DIR Accesi
            vel_left = 150; vel_right = 50;
            led_smd_val = 0x2;
            break;
        case 'i': // Pivot Right (MOTORE DX FERMO -> SPENGO DIR DX)
            xil_printf("CMD: Pivot DX (Ruota ferma)\r\n");
            // Bit 0 (Dir B - DX) = 0, Bit 2 (Dir A - SX) = 1
            current_direction_mask = 0x4; // <--- MODIFICA QUI
            vel_left = 150; vel_right = 0;
            led_smd_val = 0x2;
            break;

        default:
            break;
    }

    *gpio_leds_data = led_smd_val;
}

// Timer Setup
int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
{
      XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, 0);
      XTmrCtr_SetLoadReg(TmrCtrBaseAddress, TmrCtrNumber, 2000);
      XTmrCtr_LoadTimerCounterReg(TmrCtrBaseAddress, TmrCtrNumber);
      u32 ControlStatus = XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK;
      XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, ControlStatus);
      XTmrCtr_Enable(TmrCtrBaseAddress, TmrCtrNumber);
      return XST_SUCCESS;
}

// Interrupt PWM
void myISR(void)
{
     unsigned p = *IISR;
     if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {
         pwm_count++;
         u8 count_byte = (u8)pwm_count;

         int speed_out = current_direction_mask;

         if (count_byte < vel_left)  speed_out |= 0x8;
         if (count_byte < vel_right) speed_out |= 0x2;

         *gpio_motors_data1 = speed_out;

         u32 ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR,0);
         XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, 0, ControlStatus | XTC_CSR_INT_OCCURED_MASK);
         *IIAR  = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
     }
}
