/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */
/*
#include "mbed.h"
#include "platform/mbed_thread.h"


// Blinking rate in milliseconds
#define BLINKING_RATE_MS                                                    500


int main()
{
    // Initialise the digital pin LED1 as an output
    DigitalOut led(LED1);

    while (true) {
        led = !led;
        thread_sleep_for(BLINKING_RATE_MS);
    }
}
*/

/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

// CONSIDERACIONES 
// En MBED variables tipo: 
// - (int) son con signo y de 32 bits
// - (char) son sin signo y de 8 bits
// - (short) son con signo y de 16 bits
// - (long) son con signo y de 64 bits
// - (float) de 32 bits
// - (double) de 64 bits

// NOTAS:
// Motor A2520, puente H conectado

#include "mbed.h"
#include "Adafruit_SSD1306.h"
#include <string>

//#define     MUESTREO_POTENCIOMETRO
#define     COMANDO_PSERIE

void ControlPID_ec_dif(void);

// Blinking rate in milliseconds
#define BLINKING_RATE     500ms

#ifdef MUESTREO_POTENCIOMETRO
Serial PSerie(USBTX, USBRX, 115200);      // Mensajes hacia el puerto serie
#else
//RawSerial PSerie(USBTX, USBRX, 115200);      // Mensajes hacia el puerto serie
RawSerial PSerie(p9,p10,115200);
#endif

// Pantalla
I2C i2c(p28,p27);
Adafruit_SSD1306_I2c oled(i2c, p29, 0x78, 64, 128); // SSD1306 I2C 128x64, with no reset pin


// Motor
#define PeriodoPWM_us   500
PwmOut MotorPWM(p21);
DigitalOut MotordirA(p22);
DigitalOut MotordirB(p23);

// Encoder
#define PPV     16          // Pulsos por vuelta
#define samples 4
InterruptIn MEnc(p30);      // Contar los flancos del encoder
Timer t;                    // Contar el tiempo entre flancos
Ticker muestreo_enc;
int cont_pulsos=0;
int rpm;
DigitalOut Enc(LED2);
int sample_time[samples];         // muestras de tiempo en [us]


// Control
DigitalOut control_LED(LED4);
AnalogIn SetPoint_IN(p20);
#define k_p     0.0045054f    
#define k_i     0.0718978f  
#define k_d    -2.1058567f

int ActualSpeed, DesiredSpeed;      // En RPMs 
float error_control,p_error,a_error,d_error;
float proporcional,integral,derivativo;
float salida_pid;
char c_derivativo;
char direccion;

//#define kp  1.0f
//#define ki  5.0f
//#define kd  0.001f

// Control de velocidad con RW de aluminio
//#define kp  0.103856959526641f
//#define ki  0.061248697934551f
//#define kd  0.016174064913401f

#define kp  5.19f
#define ki  30.60f
#define kd  0.08085f


#define Ts  0.01f       // tiempo de muestreo, 10 ms
#define divisor 511.0f  // resolucion del PWM (9bits)

float error_0, error_1, error_2;
float u,u_1,q_0,q_1,q_2;

bool flag_muestra;
bool flag_comando;
bool flag_control;

void OLED_init(void) {
    i2c.frequency(400000);

    oled.setRotation(0);
    oled.clearDisplay();
    oled.splash();
    oled.display();
    wait_us(2000000);

    oled.setRotation(0);
    oled.clearDisplay();
    oled.drawRect(0,0,oled.width(),oled.height()/4,1); 
    oled.display();

    oled.setTextSize(1);
    oled.setTextCursor(20,6);
    oled.printf("Sistema ADCS");
    oled.setTextCursor(0,16);
    oled.printf("Iniciando ...\n");
    oled.display();
    wait_us(2000000);
}

void OLED_print_control(void) {
    oled.clearDisplay();
    oled.drawRect(0,0,oled.width(),oled.height()/4,1);
    oled.setTextCursor(10,5);
    oled.printf("SP = %u",DesiredSpeed);
    oled.setTextCursor(10,20);
    oled.printf("Vel= %u\n",rpm);
    //oled.writeChar('a');
    oled.display();
    //wait_us(100000);
}

void MotorMode(int modo) {
    // Modo de giro del motor
    switch(modo) {
        case 1:  // STOP
            MotordirA = 0;
            MotordirB = 0;
            break;
        case 2:  // CCW (Izquierda)
            MotordirA = 0;
            MotordirB = 1;
            break;
        case 3:  // CW (Derecha) 
            MotordirA = 1;
            MotordirB = 0;
            break;
        case 4:  // BREAK
            MotordirA = 1;
            MotordirB = 1;
            break;
        default:
            break;
    }
}

void init_Motor(void) {
    DesiredSpeed = 2500; // RPMs
    MotorMode(1);
    ThisThread::sleep_for(2000);    
    MotorPWM.period_us(PeriodoPWM_us);   // periodo del PWM
    MotorMode(3);
    direccion = 'D';
    MotorPWM.write(0.1f);
    oled.printf("Iniciando motor...\n");
    oled.display();
}

void arranque_Motor(void) {
    MotorPWM.period_us(PeriodoPWM_us);   // periodo del PWM
    MotorPWM.write(0.1f);
    t.start(); 
    //oled.printf("Iniciando motor...\n");
    //oled.display();
}

void conteo_flancos(void) {
    int temp,i;

    //temp = t.elapsed_time().count();
    temp = t.read_us();
    t.reset();
    cont_pulsos++;

    for (i=samples-1; i>0; i--)
        sample_time[i] = sample_time[i-1];     
    //sample_time[3] = sample_time[2]; 
    //sample_time[2] = sample_time[1]; 
    //sample_time[1] = sample_time[0]; 
    sample_time[0] = temp;
}

void muestreo_encoder(void) {
    int i, suma_tiempos, t_promedio;
    Enc = !Enc;
    //rpm = cont_pulsos*60/PPV;
    cont_pulsos = 0;
    flag_muestra = true;
    
    // por tiempo 
    suma_tiempos = 0;
    for(i=0; i<samples; i++)
        suma_tiempos += sample_time[i];

    if (DesiredSpeed > 700)
        t_promedio = suma_tiempos/samples;    // en microsegundos
    else
        t_promedio = sample_time[0];
    rpm = 60000000 / (t_promedio*PPV);

    ControlPID_ec_dif();
}

void InitControlPID(void)  {
    int i;
    // DesiredSpeed = 60000000/4000;   // en microsegundos
    // DesiredSpeed = 2500; // RPMs
    
    // PID (propio)
    error_control = 11;
    a_error = 0;
    d_error = 0;
    p_error = 0;
    c_derivativo = 0;

    // PID ecuaciones en diferencias
    q_0 = k_p;
    q_1 = k_i;
    q_2 = k_d;
    error_0 = 0;
    error_1 = 0;
    error_2 = 0;
    u_1 = 0; 

    oled.printf("Inicia control PID...\n");
    oled.display();
    wait_us(1000000);  // pausa de 1 segundo
}

// Control PID por ecuaciones en diferencias
void ControlPID_ec_dif(void)  {
    float salida;
    int diferencia;
    
    ActualSpeed = rpm; 
    error_0 = (float)DesiredSpeed - (float)ActualSpeed;

    // ---- Experimental -----
    if (DesiredSpeed == 0) {
        MotorMode(1);
    }
    else {
        // Estrategia de frenado
        if (error_0 < 0) {
            diferencia = ActualSpeed - DesiredSpeed;
            if(diferencia > 100) {  // el freno actua solo si hay una diferencia mayor a 100 rpm
                if(direccion == 'I') {
                    MotorMode(3);  // cambio a sentido inverso
                }
                if(direccion == 'D') {
                    MotorMode(2);
                }
            }
        }

        if (error_0 > 0) {
            if(direccion == 'I') {
                MotorMode(2);  // sentido de giro original
            }
            if(direccion == 'D') {
                MotorMode(3);
            }
        }
    }
    // -----------------------

    // Constantes
    q_0 = kp + kd/Ts;
    q_1 = -kp + ki*Ts - 2*kd/Ts;
    q_2 = kd/Ts;

    // Ecuacion en diferencias
    u = u_1 + q_0*error_0 + q_1*error_1 + q_2*error_2;

    if (u >= 500.0f)
        u = 500.0f;
    
    if (u <= 5.0f || DesiredSpeed == 0 )
        u = 5.0f;

    // Guarda los errores anteriores y la salida de control
    error_2 = error_1;
    error_1 = error_0;
    u_1 = u;

    salida = u / 500.0f;
    MotorPWM.write(salida);
    control_LED = !control_LED;        
    //flag_control_entry = true;
}



// Espera un comando recibido
#define    n_datos      10
char comandoRX[n_datos];
DigitalOut LED_PSerie(LED3);

void Interrupcion_PSerieRX (void){
    int i;
    char datoRX;

    i=0;
    do {
        datoRX = PSerie.getc();
        comandoRX[i] = datoRX;
        i++;
        if(i>(n_datos-1)) {
            flag_comando = false;
            break;
        }
        LED_PSerie = !LED_PSerie;
    }while(datoRX != '\n');

    if(comandoRX[0] == '$') flag_comando = true;
}



/*********** MAIN ************/
int main()
{
    int i,conteo;
    int cont_pantalla = 0;
    int16_t CTn;
    uint16_t CT;
    float salida_PWM;

    DigitalOut led(LED1);    // LED1 como muestra
    string mensaje="Hola";
    
    OLED_init();    // Inicia pantalla OLED
    flag_muestra = false;

    MEnc.rise(&conteo_flancos);   // interrupcion en flancos de subida
    DesiredSpeed = 0; // RPMs

    #ifdef MUESTREO_POTENCIOMETRO
    init_Motor();       // Detiene al motor y arranque inicial
    t.start();          // Inicia el timer para el conteo entre flancos
    wait_us(1000000);   // Espera a llenar el buffer de muestreo ...

    InitControlPID();   // Incializacion de variables
    muestreo_enc.attach_us(&muestreo_encoder, 10000);  //10ms, muestreo y control
    #endif

    PSerie.attach(&Interrupcion_PSerieRX, Serial::RxIrq);   // Interrupcion por puerto serie
    flag_control = false;
    //oled.setTextSize(2);
    
    while (true) {
        led = !led;
        //ThisThread::sleep_for(BLINKING_RATE);
        //printf("Hola Mundo!!\n\r");

        #ifdef MUESTREO_POTENCIOMETRO
        if(flag_muestra) {
            flag_muestra = false;
            DesiredSpeed = (int) (SetPoint_IN.read() * 260.0f);
            DesiredSpeed *= 20;
            printf("$%d %d;", rpm, DesiredSpeed);
            cont_pantalla++;
            //OLED_print_control();
            conteo = 50;
        }
        #endif

        #ifdef COMANDO_PSERIE
        if(flag_comando) {
            flag_comando = false;

            //for(i=0;i<10;i++) PSerie.putc(comandoRX[i]);
            
            i=0;            
            while(comandoRX[i] != '$') {   // Encuentra el encabezado en el comando
                i++;
                if (i>n_datos-1) break;
            }

            //PSerie.putc(comandoRX[i]);
            i++;
            switch(comandoRX[i]) {
                case 'I':       // giro Izquierda
                    direccion = 'I';
                    PSerie.putc(0x4B);  // K, comando aceptado
                    MotorMode(2);
                    DesiredSpeed = comandoRX[i+1]*256 + comandoRX[i+2];
                    if (!flag_control) {
                        flag_control = true;
                        arranque_Motor();
                        InitControlPID();
                        muestreo_enc.attach_us(&muestreo_encoder,10000);
                    }
                    break;

                case 'D':       // giro derecha
                    direccion = 'D';
                    PSerie.putc(0x4B);
                    MotorMode(3);
                    DesiredSpeed = comandoRX[i+1]*256 + comandoRX[i+2];
                    if (!flag_control) {
                        flag_control = true;
                        arranque_Motor();
                        InitControlPID();
                        muestreo_enc.attach_us(&muestreo_encoder,10000);
                    }
                    break;

                case 'S':       // ALTO
                    PSerie.putc(0x4B);
                    t.stop();
                    t.reset();
                    MotorMode(1);
                    flag_control = false;
                    muestreo_enc.detach();
                    MotorPWM.write(0);
                    break;

                case 'B':       // FRENO
                    PSerie.putc(0x4B);
                    t.stop();
                    t.reset();
                    MotorMode(4);
                    flag_control = false;
                    muestreo_enc.detach();
                    break;      

                case 'i':
                    PSerie.putc(0x4B);
                    MotorMode(2); 
                    CT = comandoRX[i+1]*256 + comandoRX[i+2];
                    salida_PWM = (float)CT/divisor;
                    MotorPWM.write(salida_PWM);
                    rpm = CT;
                    break;
                
                case 'd':
                    PSerie.putc(0x4B);
                    MotorMode(3); 
                    CT = comandoRX[i+1]*256 + comandoRX[i+2];
                    salida_PWM = (float)CT/divisor;
                    MotorPWM.write(salida_PWM);
                    rpm = CT;
                    break;       

                case 'c':
                    PSerie.putc(0x4B);
                    CTn = comandoRX[i+1]*256 + comandoRX[i+2];
                    if(CTn < 0) {
                        MotorMode(2);
                        salida_PWM = -1*(float)CTn / divisor;
                    }
                    else {
                        MotorMode(3);
                        salida_PWM = (float)CTn / divisor;
                    }
                    MotorPWM.write(salida_PWM);
                    rpm = CT;
                    break;   
                
                default:
                    PSerie.putc(0x4E);  // N, comando erroneo
                    break;
            }
            OLED_print_control();            
        }
        cont_pantalla++;
        conteo = 150000;
        #endif

        if(cont_pantalla > conteo) {
            cont_pantalla = 0;
            OLED_print_control();
        }
        
    }
}

