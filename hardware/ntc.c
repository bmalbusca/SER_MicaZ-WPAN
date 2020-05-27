#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include<time.h>

#define VCC 5
#define R1 3300
#define R2 3300
#define R3 3000
#define BETA 3380
#define RTO 10000
#define TO 293.15 //298.15

#define VO 3




float read_temp(double Ro, double To_Ro, double Beta){

 
 double variance =  (VO + (rand() % VO));
  printf(" var %f \n", variance);

 double aux_Wbridge = ((double)(variance)/(double)VCC) + (double)(R2/(double)(R2+R1));
  printf(" aux %f \n", aux_Wbridge);
  double Rt = R3* aux_Wbridge /(1+ aux_Wbridge);
  printf("Resistance %f\n", Rt);
  return  (float)((1/((1/(double)To_Ro) + log(Rt/(double)Ro)/(double)Beta)) - 273.15);    // Temperature in Celsius


}

int main(void){
    srand(time(NULL));
    while(1){
        printf("temperature  %f \n", read_temp(RTO,TO, BETA));
    
        sleep(2);
    }

    return 0;
}


