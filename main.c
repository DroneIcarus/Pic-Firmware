/******************************************************************************/
/* Files to Include                                                           */
/******************************************************************************/
   

#include <xc.h>         /* XC8 General Include File */

#include <stdint.h>        /* For uint8_t definition */
#include <stdbool.h>       /* For true/false definition */

#define SWITCH_ENABLE       LATAbits.LATA0  // Valid for Icarus V.01  
#define SWITCH_FASTTURNOFF  LATAbits.LATA1  // Valid for Icarus V.01
//#define SWITCH_ENABLE       LATAbits.LATA4  // Valid for Icarus mini  
//#define SWITCH_FASTTURNOFF  LATAbits.LATA5  // Valid for Icarus mini
#define SWITCH_CHARGEPUMP   LATAbits.LATA2
#define DISABLE_CHARGER     LATAbits.LATA3
#define ESC_MONITOR         PORTBbits.RB3
#define STAT1               PORTCbits.RC5
#define STAT2               PORTCbits.RC6
#define BATMON_ALERT        PORTCbits.RC1

#define SWITCH_ENABLE_TRIS      TRISAbits.TRISA0 // Valid for Icarus V.01
#define SWITCH_FASTTURNOFF_TRIS TRISAbits.TRISA1 // Valid for Icarus V.01
//#define SWITCH_ENABLE_TRIS      TRISAbits.TRISA4 // Valid for Icarus mini
//#define SWITCH_FASTTURNOFF_TRIS TRISAbits.TRISA5 // Valid for Icarus mini
#define SWITCH_CHARGEPUMP_TRIS  TRISAbits.TRISA2
#define DISABLE_CHARGER_TRIS    TRISAbits.TRISA3

#define STAT1_TRIS               TRISCbits.TRISC5
#define STAT2_TRIS               TRISCbits.TRISC6
#define BATMON_ALERT_TRIS        TRISCbits.TRISC1
#define ESC_MONITOR_TRIS        TRISBbits.TRISB3


#define I2C_SCL_TRIS            TRISBbits.TRISB1
#define I2C_SDA_TRIS            TRISBbits.TRISB2

#define LOW_POWER 0
#define HIGH_POWER 1

#define ESC_MONITOR_DC_MAX_FOR_DODO 0.053
#define CHARGE_IN_PROGRESS    (!STAT1 && STAT2)
#define CHARGE_COMPLETE       (STAT1 && !STAT2)
#define CHARGE_SUSPEND        (STAT1 && STAT2)

/******************************************************************************/
/* User Global Variable Declaration                                           */
/******************************************************************************/

/* i.e. uint8_t <variable_name>; */

unsigned char Switch_State; 
unsigned char Switch_Turnon_Request;
unsigned char Switch_Turnoff_Request;
unsigned char OSC_Lo_Request;
unsigned char OSC_Hi_Request;
unsigned int ESC_period_low;
unsigned int ESC_period_high;
unsigned char ESC_period_over;
unsigned int  ESC_valid_consecutive_periods_counter; 
unsigned int ESC_period_high_bkp;
unsigned int ESC_period_low_bkp;
unsigned int ESC_period_total;
unsigned char I2C_dataCounter;
unsigned char I2C_data[16];
unsigned char I2C_receiving;
unsigned char turnoffAllowed;
float maxDodo;
float duty_cycle; 
unsigned long temps_on;
unsigned long temps_off;


void Turnoff(void);
void Turnon(void);
void Switch_Manage(void);
void ESC_monitor_Manage(void);
void I2C_Manage(void);
bool charge_done(void);
void ESC_monitor_Turnoff(void);
void ESC_monitor_Turnon(void);
void goToBed(unsigned long long timer);
void setOscillator(unsigned char mode);
void Switch_Turnoff(void);
void Switch_Turnon(void);
void I2C_Turnon(void);



void I2C_Turnon(void)
{
    I2C_SCL_TRIS = 1;
    I2C_SDA_TRIS = 1;
    
    SSP2STATbits.SMP = 0;
    SSP2CON1bits.SSPM = 0xe;
    SSP2CON2bits.SEN = 0;
    SSP2CON3bits.AHEN = 0; 
    SSP2CON3bits.DHEN = 0;
    SSP2CON3bits.SCIE = 0;
    SSP2CON3bits.PCIE = 1;
    SSP2CON3bits.BOEN = 1;
    SSP2CON1bits.CKP = 1;
    PIR3bits.SSP2IF = 0;
    PIE3bits.SSP2IE = 1;
    SSP2ADD = 0x12<<1;
    SSP2MSK = 0xFF;
    ANSELB = 0;
    SSP2CON1bits.SSPEN = 1;
}

void Switch_Turnon(void)
{
    Switch_State = 1;
    for(unsigned char i = 0; i < 10; i++)
        SWITCH_CHARGEPUMP = !SWITCH_CHARGEPUMP; //make charge pump oscillate
    SWITCH_ENABLE = 1;
    
    I2C_Turnon();
}

void Switch_Turnoff(void)
{
    SWITCH_CHARGEPUMP = 0;
    SWITCH_ENABLE = 0;
    SWITCH_FASTTURNOFF = 1;
    Switch_State = 0;
    for (unsigned char i = 0; i<255; i++);
    SWITCH_FASTTURNOFF = 0;
}

void setOscillator(unsigned char mode)
{
    if(mode == LOW_POWER)
    {
        OSCCON1bits.NOSC = 0x5; //LFINTOSC, ~32khz //~80uA en low power sans etre en sleep, 60uA en sleep (board complet))
    }
    else if(mode == HIGH_POWER)
    {
        OSCCON1bits.NOSC = 0x6; //HFINTOSC, ~16MHz //~350uA en high power sans etre en sleep
        OSCFRQ = 0x5;
    }
}

void goToBed(unsigned long long timer)
{
    if(!CHARGE_COMPLETE)
    {
        Turnoff();
        WDTCON1bits.WDTCS = 0x0;
        WDTCON1bits.WINDOW = 0x7;
        WDTCON0bits.WDTPS = 0xa; //~1s par timeout
        setOscillator(LOW_POWER);
        WDTCON0bits.SWDTEN = 1; //active watchdog

        unsigned long long i = 0;
        while(i < timer && !CHARGE_COMPLETE)
        {
            SLEEP();
            i++;
        }

        WDTCON0bits.SWDTEN = 0; //active watchdog
        setOscillator(HIGH_POWER);
        Turnon();
    }
}

void ESC_monitor_Turnon(void)
{
    ESC_MONITOR_TRIS = 1;

    ANSELB = 0;
    IOCBP |= 0x08;
    IOCBN |= 0x08;
    PIR0bits.IOCIF = 0;
    PIE0bits.IOCIE = 1;
    ESC_period_low = 0;
    ESC_period_high = 0;
    
    T0CON0bits.T0EN = 0;
    T0CON0bits.T016BIT = 1;
    T0CON1bits.T0CS = 2;
    T0CON1bits.T0CKPS = 2;
    TMR0H = 0xD0; 
    TMR0L = 0x00; 
    
    PIR0bits.TMR0IF = 0;
    PIE0bits.TMR0IE = 1;
    T0CON0bits.T0EN = 1;  //dans cette configuration, overflow tous les 32ms environ.    

    ESC_valid_consecutive_periods_counter = 0;
    ESC_period_over = 0;

}

void ESC_monitor_Turnoff(void)
{
    PIE0bits.IOCIE = 0;
    PIE0bits.TMR0IE = 0;
    T0CON0bits.T0EN = 0;  //dans cette configuration, overflow tous les 32ms environ.    
}

void I2C_Manage(void)
{

}

void ESC_monitor_Manage(void)
{
    if(ESC_period_over == 1)
    {
        ESC_period_high_bkp = ESC_period_high - 0xD000;
        ESC_period_low_bkp = ESC_period_low - 0xD000;
        ESC_period_total = ESC_period_high_bkp + ESC_period_low_bkp;

        ESC_period_over = 0;

        temps_on = ESC_period_high_bkp;
        temps_off = ESC_period_low_bkp; 
        duty_cycle = (float)temps_on / (float)temps_off; //8192 est 100%

        if(duty_cycle <= maxDodo) //PWM freq. entre 40Hz et 200Hz, periode high entre 1.11ms et 909us pour �tre "valide"
        {
            if(ESC_valid_consecutive_periods_counter<50) //saturation
            {
                ESC_valid_consecutive_periods_counter++;
                turnoffAllowed = 0;
            }
            else
            {
                ESC_valid_consecutive_periods_counter = 50;
                turnoffAllowed = 1;//TURNOFF IS ALLOWED         
            }
        }
        else
        {
            ESC_valid_consecutive_periods_counter = 0;
            turnoffAllowed = 0;        //TURNOFF IS DISALLOWED
        }
    }
}

void Switch_Manage(void)
{          
    if(Switch_State == 1)
    {
        SWITCH_CHARGEPUMP = !SWITCH_CHARGEPUMP; //make charge pump oscillate
    }
}

void Turnon(void)
{
    PMD0 = 0x7e;
    PMD1 = 0xfe;
    PMD2 = 0xff;
    PMD3 = 0xff;
    PMD4 = 0xdf;
    PMD5 = 0xff;
    OSCFRQ = 5;
    VREGCONbits.VREGPM = 1;

    Switch_Turnon();
    I2C_Turnon();
    ESC_monitor_Turnon();
}

void Turnoff(void)
{
    ESC_monitor_Turnoff();
    Switch_Turnoff();
    PMD0 = 0x7f;
    PMD1 = 0xff;
    PMD2 = 0xff;
    PMD3 = 0xff;
    PMD4 = 0xff;
    PMD5 = 0xff;
    OSCFRQ = 2;
    VREGCONbits.VREGPM = 1; 
}



/******************************************************************************/
/* Main Program                                                               */
/******************************************************************************/
void main(void)
{   
    maxDodo = ESC_MONITOR_DC_MAX_FOR_DODO;

    SWITCH_ENABLE = 0;
    SWITCH_FASTTURNOFF = 0;
    SWITCH_CHARGEPUMP = 0;
    DISABLE_CHARGER = 0;
    
    SWITCH_ENABLE_TRIS = 0;
    SWITCH_FASTTURNOFF_TRIS = 0;
    SWITCH_CHARGEPUMP_TRIS = 0;
    DISABLE_CHARGER_TRIS = 0;
    
    STAT1_TRIS = 1;
    STAT2_TRIS = 1;
    BATMON_ALERT_TRIS = 1;
    
    ANSELC = 0; 
    
    turnoffAllowed = 0;
    
    Turnon();
    
    OSCFRQ = 5;
    GIE = 0;
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCK = 0x00; // unlock PPS

    RB1PPS = 0x17;    // RB1->I2C2SCL
    RB2PPS = 0x18;    // RB2->I2C2SDA
    
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCK = 0x01; // lock   PPS

    
    PEIE = 1;    
    GIE = 1;
    I2C_Turnon();
    Turnoff();
    Turnon();
    
    while(1)
    {
        I2C_Manage();
        ESC_monitor_Manage();
        Switch_Manage(); 
    }
}

void __interrupt () isr(void)
{
    if(PIR0bits.TMR0IF == 1)
    {
        PIR0bits.TMR0IF = 0;
        TMR0H = 0xd0; 
        TMR0L = 0x00;
        ESC_valid_consecutive_periods_counter = 50; //TURNOFF IS _ALLOWED_
        turnoffAllowed = 1;
    }
    
    if(PIR0bits.IOCIF == 1)
    {
        PIR0bits.IOCIF = 0;
                
        if(IOCBFbits.IOCBF3 == 1)
        {
            IOCBFbits.IOCBF3 = 0;
            T0CON0bits.T0EN = 0;
            if(ESC_MONITOR == 0)
            {
                //falling edge detected
                ESC_period_high = TMR0H << 8 | TMR0L;
            }
            else{
                ESC_period_low = TMR0H << 8 | TMR0L;
                ESC_period_over = 1;
            }
//            TMR0H = 0xE0; 
            TMR0H = 0xd0;
            TMR0L = 0x00;
            T0CON0bits.T0EN = 1;
        }
    }
    
    if(PIR3bits.SSP2IF)
    {
        unsigned char bidon = 0;
        PIR3bits.SSP2IF = 0;
        bidon = SSP2BUF;
        SSP2CON1bits.CKP = 1;
        if(SSP2STATbits.D_nA == 0 && bidon == (SSP2ADD & 0xFE))
        {
            I2C_receiving = 1;
            I2C_dataCounter = 0;
        }
        else if(SSP2STATbits.D_nA == 0 && bidon != (SSP2ADD & 0xFE)) 
        {
            I2C_receiving = 0;
        }
        
        if(I2C_receiving && SSP2STATbits.DA == 1)
        {
            I2C_data[I2C_dataCounter++] = bidon;
        }

        if(I2C_receiving && SSP2STATbits.DA == 1 && SSP2STATbits.P == 1)
        {
            I2C_receiving = 0;
            //execute
            if(I2C_dataCounter == 5 && I2C_data[0] == 0x31)
            {//verification que sleep mode is allowed
                if(turnoffAllowed == 1)
                    goToBed((unsigned long long)I2C_data[1]<<24 | (unsigned long long)I2C_data[2]<<16 | (unsigned long long)I2C_data[3]<<8 | (unsigned long long)I2C_data[4]<<0);
            }
        }
    }
}

