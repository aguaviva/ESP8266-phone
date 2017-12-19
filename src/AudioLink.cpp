#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>

#include <DNSServer.h>
#include <ESPAsyncWiFiManager.h>         //https://github.com/tzapu/WiFiManager

#include "openlpc.h"
#include "AudioIn.h"
#include "AudioOut.h"
#include "adc3201.h"

#include <i2s.h>


#define OTA
#define HOST_NAME "esp-audio-link"
#define HTTP_USERNAME  "admin"
#define HTTP_PASSWORD "admin"

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

DNSServer dns;

bool AudioIsRaw = true;


static openlpc_encoder_state *encoder_st=NULL;

short ReadMic()
{
  return adcRead();//-2048;
}

int connectedClients = 0;

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
  if(type == WS_EVT_CONNECT)
  {
    client->ping();
    if (connectedClients==0)
    {
        adcSetup();
        //aiBegin(ReadMic,10);
        aoBegin(8000);
    }
    connectedClients++;
  }
  else if(type == WS_EVT_DISCONNECT)
  {
    connectedClients--;
    if (connectedClients==0)
    {
        //aiEnd();
        aoEnd();
    }
  }
  else if(type == WS_EVT_ERROR)
  {
    //Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  }
  else if(type == WS_EVT_PONG)
  {
    //Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  }
  else if(type == WS_EVT_DATA)
  {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if(info->final && info->index == 0 && info->len == len)
    {
        if(info->opcode == WS_BINARY)
        {
            int16_t av = i2s_available();

            int16_t written = aoQueue((short *)data, len/2);
            ESP.wdtFeed();
            //ws.printfAll("w: %i, %i, %i", av, i2s_available(), written);
        }
    }
  }
}

void setup()
{
  Serial.begin(115200);

  // Wait for connection
  AsyncWiFiManager wifiManager(&server,&dns);
  wifiManager.autoConnect("AutoConnectAP");

  //Send OTA events to the browser
  #ifdef OTA
    ArduinoOTA.setHostname(HOST_NAME);
    ArduinoOTA.begin();
    Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", HOST_NAME);
  #endif

  // Start MSDN
  //
  if ( MDNS.begin ( HOST_NAME ) )
  {
      MDNS.addService("http", "tcp", 80);
      Serial.println ( "MDNS responder started" );
  }

  // Start Spiffs
  //
  {
      SPIFFS.begin();
      server.addHandler(new SPIFFSEditor(HTTP_USERNAME, HTTP_PASSWORD));
      server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
      Serial.println ( "SPIFFS started" );
  }

  encoder_st = create_openlpc_encoder_state();
  init_openlpc_encoder_state(encoder_st, 160);
  Serial.println("Starting encoder");


  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/mode.html", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if(request->hasParam("cmd"))
    {
      AsyncWebParameter* p = request->getParam("cmd");
      if (p!=NULL)
      {
        if (p->value()=="plc")
        {
          request->send(200, "text/plain", "openplc mode");
          AudioIsRaw = false;
        }
        else if (p->value()=="raw")
        {
          request->send(200, "text/plain", "raw mode");
          AudioIsRaw = true;
        }
        else
        {
          request->send(200, "text/plain", "choose raw or plc");
        }
      }
    }
  });

  server.on("/playSin", HTTP_GET, [](AsyncWebServerRequest *request)
  {
      PlaySin(8000);
      request->send(200, "text/plain", "ok");
  });

  server.on("/playHola", HTTP_GET, [](AsyncWebServerRequest *request)
  {
      PlayAudio(8000);
      request->send(200, "text/plain", "ok");
  });

  server.on("/encoded.bin", HTTP_GET, [](AsyncWebServerRequest *request)
  {
      //Serial.printf("compressing hola.raw and sending it..\n");
      openlpc_encoder_state *st = create_openlpc_encoder_state();
      if (st==NULL)
      {
        request->send(200,"cant create openlpc_encoder_state");
        return;
      }
      init_openlpc_encoder_state(st, 160);

      fs::File f = SPIFFS.open("/hola.raw", "r");

      //Serial.printf("opened:%i bytes\n",f.size());

      unsigned char params[7*100];

      int i=0;
      for(;;i+=7)
      {
        short data[160];
        int size = f.readBytes((char*)data, 160*2);
        ESP.wdtFeed();
        if (size<160)
          break;

        openlpc_encode(data, &params[i], st);
        ESP.wdtFeed();
      }

      //destroy_openlpc_encoder_state(st);
      f.close();
      request->send_P(200, "application/octet-stream", params, i);
  });

  server.onNotFound([](AsyncWebServerRequest *request)
  {
      String message = "File Not Found\n\n";
      message += "URI: ";
      message += request->url();
      //message += "\nMethod: ";
      //message += (server.method() == HTTP_GET)?"GET":"POST";
      message += "\nArguments: ";
      message += request->args();
      message += "\n";
      for (uint8_t i=0; i<request->args(); i++){
      message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
      }
      request->send(404, "text/plain", message);
  });

  // Start the webserver
  //
  server.begin();
  Serial.println("Webserver started ");

  //Serial.flush();
  delay(100);
  Serial.end();

  PlayAudio2();

}

void loop()
{
   if (connectedClients>0)
   {
     short *data = aiLock();
     if (data!=NULL)
     {
        if (AudioIsRaw==false)
        {
          unsigned char params[7];
          openlpc_encode(data, params, encoder_st);
          ws.binaryAll((char*)params,7);
        }
        else
        {
          ws.binaryAll((char*)data,160*2);
        }
        aiUnlock();

        ESP.wdtFeed();
     }
   }

   MDNS.update();
   ArduinoOTA.handle();
}
