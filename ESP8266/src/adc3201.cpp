#include <Arduino.h>

#define ADC_CLK 16
#define ADC_D    5
#define ADC_CS   4

void adcSetup()
{
  pinMode(ADC_CLK, OUTPUT);      // sets the digital pin 13 as output
  pinMode(ADC_D, INPUT);
  pinMode(ADC_CS, OUTPUT);      // sets the digital pin 13 as output
}

short adcRead()
{
  digitalWrite(ADC_CS, LOW);

  digitalWrite(ADC_CLK, LOW);

  digitalWrite(ADC_CLK, HIGH );
  digitalWrite(ADC_CLK, LOW);

  digitalWrite(ADC_CLK, HIGH );
  digitalWrite(ADC_CLK, LOW);

  unsigned short v=0;
  for(int i=0;i<13;i++)
  {
    digitalWrite(ADC_CLK, HIGH );


    v = (v <<1)  | digitalRead(ADC_D);

    digitalWrite(ADC_CLK, LOW);
  }
  digitalWrite(ADC_CS, HIGH );

  return v;
}
