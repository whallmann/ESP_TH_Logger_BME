/*---------------------------------------------------
HTTP 1.1 Temperature & Humidity Webserver for ESP8266 
for ESP8266 adapted Arduino IDE

by Stefan Thesen 05/2015 - free for anyone
Changed by Stephan Forth 12/2017
Changed by Wolfgang  03/2021

Connect DHT22 / AM2302 at GPIO2 = D4
Connect BME to D1 = SCL, D2=SDA
---------------------------------------------------*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "time_ntp.h"
//#include "DHT.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;



// WiFi connection
const char* ssid = "hallmann5";
const char* password = "wsunH61!";

// ntp timestamp
unsigned long ulSecs2000_timer=0;

// storage for Measurements; keep some mem free; allocate remainder
#define KEEP_MEM_FREE 10240
#define MEAS_SPAN_H 84
unsigned long ulMeasCount=0;    // values already measured
unsigned long ulNoMeasValues=0; // size of array
unsigned long ulMeasDelta_ms;   // distance to next meas time
unsigned long ulNextMeas_ms;    // next meas time
unsigned long *pulTime;         // array for time points of measurements
float *pfTemp,*pfHum, *pfPres;           // array for temperature and humidity measurements

unsigned long ulReqcount;       // how often has a valid page been requested
unsigned long ulReconncount;    // how often did we connect to WiFi
byte retry = 4;                 // Variable für den NAN Zaehler

//============================================================================
String Headertext = "DF7PN Sensor-3";
//============================================================================

// Create an instance of the server on Port 80
WiFiServer server(80);

//////////////////////////////
// DHT21 / AMS2301 is at GPIO2
//////////////////////////////
//#define DHTPIN D4

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11 
//#define DHTTYPE DHT22   // DHT 22  (AM2302)
//#define DHTTYPE DHT22   // DHT 22 (AM2302)

// init DHT; 3rd parameter = 16 works for ESP8266@80MHz
//DHT dht(DHTPIN, DHTTYPE);

// needed to avoid link error on ram check
extern "C" 
{
#include "user_interface.h"
}

/////////////////////
// the setup routine
/////////////////////
void setup() 
{
  // setup globals
  ulReqcount=0; 
  ulReconncount=0;
    
  // start serial
  Serial.begin(115200);
  Serial.println("Debug Ausgaben: ");
  // start BME280 Sensor
  bme.begin(0x76);   
   
  // inital connect
  WiFi.mode(WIFI_STA);
  WiFi.hostname("Sensor3");  // http://sensor3.fritz.box
  WiFiStart();
  
  // allocate ram for data storage
  uint32_t free=system_get_free_heap_size() - KEEP_MEM_FREE;
  ulNoMeasValues = free / (sizeof(float)*3+sizeof(unsigned long));  // humidity & temp --> 2 + time
  pulTime = new unsigned long[ulNoMeasValues];
  pfTemp = new float[ulNoMeasValues];
  pfHum = new float[ulNoMeasValues];
  pfPres = new float[ulNoMeasValues];
  
  if (pulTime==NULL || pfTemp==NULL || pfHum==NULL || pfPres==NULL)
  {
    ulNoMeasValues=0;
    Serial.println("Error in memory allocation!");
  }
  else
  {
    Serial.print("Allocated storage for ");
    Serial.print(ulNoMeasValues);
    Serial.println(" data points.");
    
    float fMeasDelta_sec = MEAS_SPAN_H*3600./ulNoMeasValues;
    ulMeasDelta_ms = ( (unsigned long)(fMeasDelta_sec+0.5) ) * 1000;  // round to full sec
    Serial.print("Measurements will happen each ");
    Serial.print(ulMeasDelta_ms / 1000);
    Serial.println(" sec.");
    
    ulNextMeas_ms = millis()+ulMeasDelta_ms;
  }
}


///////////////////
// (re-)start WiFi
///////////////////
void WiFiStart()
{
  ulReconncount++;
  
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.println(WiFi.localIP());
  
  ///////////////////////////////
  // connect to NTP and get time
  ///////////////////////////////
  ulSecs2000_timer=getNTPTimestamp();
  Serial.print("Current Time UTC from NTP server: " );
  Serial.println(epoch_to_string(ulSecs2000_timer).c_str());

  ulSecs2000_timer -= millis()/1000;  // keep distance to millis() counter
}


/////////////////////////////////////
// make html table for measured data
/////////////////////////////////////
unsigned long MakeTable (WiFiClient *pclient, bool bStream)
{
  unsigned long ulLength=0;
  
  // here we build a big table.
  // we cannot store this in a string as this will blow the memory   
  // thus we count first to get the number of bytes and later on 
  // we stream this out
  if (ulMeasCount==0) 
  {
    String sTable = "Noch keine Daten verf&uuml;gbar.<BR>";
    if (bStream)
    {
      pclient->print(sTable);
    }
    ulLength+=sTable.length();
  }
  else
  { 
    unsigned long ulEnd;
    if (ulMeasCount>ulNoMeasValues)
    {
      ulEnd=ulMeasCount-ulNoMeasValues;
    }
    else
    {
      ulEnd=0;
    }
    
    String sTable;
    sTable = "<table style=\"width:350px\"><tr><th>Zeit / UTC</th><th>T &deg;C</th><th>Feuchte &#037;</th><th>Druck</th></tr>";
    sTable += "<style>table, th, td {border: 2px solid black; border-collapse: collapse;} th, td {padding: 5px;} th {text-align: left;}</style>";
    for (unsigned long li=ulMeasCount;li>ulEnd;li--)
    {
      unsigned long ulIndex=(li-1)%ulNoMeasValues;
      sTable += "<tr><td>";
      sTable += epoch_to_string(pulTime[ulIndex]).c_str();
      sTable += "</td><td>";
      sTable += pfTemp[ulIndex];
      sTable += "</td><td>";
      sTable += pfHum[ulIndex];
      sTable += "</td><td>";
      sTable += pfPres[ulIndex];
      sTable += "</td></tr>";

      // play out in chunks of 1k
      if(sTable.length()>1024)
      {
        if(bStream)
        {
          pclient->print(sTable);
          //pclient->write(sTable.c_str(),sTable.length());
        }
        ulLength+=sTable.length();
        sTable="";
      }
    }
    
    // remaining chunk
    sTable+="</table>";
    ulLength+=sTable.length();
    if(bStream)
    {
      pclient->print(sTable);
      //pclient->write(sTable.c_str(),sTable.length());
    }   
  }
  
  return(ulLength);
}
  

////////////////////////////////////////////////////
// make google chart object table for measured data
// ChartNbr
// 1: Temp
// 2: Hum
// 3: Pressure
////////////////////////////////////////////////////
unsigned long MakeList (WiFiClient *pclient, bool bStream, byte ChartNbr)
{
  unsigned long ulLength=0;
  
  // here we build a big list.
  // we cannot store this in a string as this will blow the memory   
  // thus we count first to get the number of bytes and later on 
  // we stream this out
  if (ulMeasCount>0) 
  { 
    unsigned long ulBegin;
    if (ulMeasCount>ulNoMeasValues)
    {
      ulBegin=ulMeasCount-ulNoMeasValues;
    }
    else
    {
      ulBegin=0;
    }
    
    String sTable="";
    for (unsigned long li=ulBegin;li<ulMeasCount;li++)
    {
      // result shall be ['18:24:08 - 21.5.2015',21.10]
      unsigned long ulIndex=li%ulNoMeasValues;
      sTable += "['";
      sTable += epoch_to_string(pulTime[ulIndex]).c_str();
      sTable += "',";
      if (ChartNbr==1)
        { sTable += pfTemp[ulIndex]; }
      else
      if (ChartNbr==2) 
        {sTable += pfHum[ulIndex];} 
      else 
        {sTable += pfPres[ulIndex];}
      sTable += "],\n";

      // play out in chunks of 1k
      if(sTable.length()>1024)
      {
        if(bStream)
        {
          pclient->print(sTable);
          //pclient->write(sTable.c_str(),sTable.length());
        }
        ulLength+=sTable.length();
        sTable="";
      }
    }
    
    // remaining chunk
    if(bStream)
    {
      pclient->print(sTable);
      //pclient->write(sTable.c_str(),sTable.length());
    } 
    ulLength+=sTable.length();  
  }
  
  return(ulLength);
}


//////////////////////////
// create HTTP 1.1 header
//////////////////////////
String MakeHTTPHeader(unsigned long ulLength)
{
  String sHeader;
  
  sHeader  = F("HTTP/1.1 200 OK\r\nContent-Length: ");
  sHeader += ulLength;
  sHeader += F("\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  
  return(sHeader);
}


////////////////////
// make html footer
////////////////////
String MakeHTTPFooter()
{
  String sResponse;
  
  sResponse  = F("<FONT Face='Arial' SIZE=-2><BR>Aufrufz&auml;hler="); 
  sResponse += ulReqcount;
  sResponse += F("<BR>Verbindungsz&auml;hler="); 
  sResponse += ulReconncount;
  sResponse += F("<BR>Freies RAM=");
  sResponse += (uint32_t)system_get_free_heap_size();
  sResponse += F("<BR>Max. Datenpunkte=");
  sResponse += ulNoMeasValues;
  sResponse += F("<BR>DF7PN 3/2021 BME280<BR></font></body></html>");
  
  return(sResponse);
}


/////////////
// main loop
/////////////
void loop() 
{
  ///////////////////
  // do data logging
  ///////////////////
  if (millis()>=ulNextMeas_ms) 
  {
    ulNextMeas_ms = millis()+ulMeasDelta_ms;

//    pfHum[ulMeasCount%ulNoMeasValues] = dht.readHumidity();
//    pfTemp[ulMeasCount%ulNoMeasValues] = dht.readTemperature();

    do
    {
      Serial.print("Try read sensor Hum: ... "); 
      pfHum[ulMeasCount%ulNoMeasValues] = bme.readHumidity();
      Serial.println(pfHum[ulMeasCount%ulNoMeasValues]);
      if (isnan(pfTemp[ulMeasCount%ulNoMeasValues])) delay(2000); else delay(200);
      Serial.print("Try read sensor Tmp: ... "); 
      pfTemp[ulMeasCount%ulNoMeasValues] = bme.readTemperature();
      Serial.println(pfTemp[ulMeasCount%ulNoMeasValues]);
      if (isnan(pfHum[ulMeasCount%ulNoMeasValues])) delay(2000); else delay(200);
      Serial.print("Try read sensor Pressure: ... "); 
      pfPres[ulMeasCount%ulNoMeasValues] = bme.readPressure() / 100.0F;
      Serial.println(pfPres[ulMeasCount%ulNoMeasValues]);
      if (isnan(pfPres[ulMeasCount%ulNoMeasValues])) delay(2000); else delay(200);
      retry-- ;
    } 
    while( ((isnan(pfTemp[ulMeasCount%ulNoMeasValues]))||
            (isnan(pfHum[ulMeasCount%ulNoMeasValues])) ||
            (isnan(pfPres[ulMeasCount%ulNoMeasValues])) && (retry > 0)) );
    if (retry <= 0) Serial.println("Timeout");
    retry = 4;
    pulTime[ulMeasCount%ulNoMeasValues] = millis()/1000+ulSecs2000_timer;
    
    Serial.print("Logging Temperature: "); 
    Serial.print(pfTemp[ulMeasCount%ulNoMeasValues]);
    Serial.print(" deg Celsius - Humidity: "); 
    Serial.print(pfHum[ulMeasCount%ulNoMeasValues]);
    Serial.print("% - Pressure: ");
    Serial.print(pfPres[ulMeasCount%ulNoMeasValues]);
    Serial.print(" mbar - Time: "); 
    Serial.println(pulTime[ulMeasCount%ulNoMeasValues]);
    
    ulMeasCount++;
  }
  
  //////////////////////////////
  // check if WLAN is connected
  //////////////////////////////
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFiStart();
  }
  
  ///////////////////////////////////
  // Check if a client has connected
  ///////////////////////////////////
  WiFiClient client = server.available();
  if (!client) 
  {
    return;
  }
  
  // Wait until the client sends some data
  Serial.println("new client");
  unsigned long ultimeout = millis()+250;
  while(!client.available() && (millis()<ultimeout) )
  {
    delay(1);
  }
  if(millis()>ultimeout) 
  { 
    Serial.println("client connection time-out!");
    return; 
  }
  
  /////////////////////////////////////
  // Read the first line of the request
  /////////////////////////////////////
  String sRequest = client.readStringUntil('\r');
  //Serial.println(sRequest);
  client.flush();
  
  // stop client, if request is empty
  if(sRequest=="")
  {
    Serial.println("empty request! - stopping client");
    client.stop();
    return;
  }
  
  // get path; end of path is either space or ?
  // Syntax is e.g. GET /?show=1234 HTTP/1.1
  String sPath="",sParam="", sCmd="";
  String sGetstart="GET ";
  int iStart,iEndSpace,iEndQuest;
  iStart = sRequest.indexOf(sGetstart);
  if (iStart>=0)
  {
    iStart+=+sGetstart.length();
    iEndSpace = sRequest.indexOf(" ",iStart);
    iEndQuest = sRequest.indexOf("?",iStart);
    
    // are there parameters?
    if(iEndSpace>0)
    {
      if(iEndQuest>0)
      {
        // there are parameters
        sPath  = sRequest.substring(iStart,iEndQuest);
        sParam = sRequest.substring(iEndQuest,iEndSpace);
      }
      else
      {
        // NO parameters
        sPath  = sRequest.substring(iStart,iEndSpace);
      }
    }
  }
    
  
  ///////////////////////////
  // format the html response
  ///////////////////////////
  String sResponse,sResponse2,sHeader;
  
  /////////////////////////////
  // format the html page for /
  /////////////////////////////
  if(sPath=="/") 
  {
    ulReqcount++;
    int iIndex= (ulMeasCount-1)%ulNoMeasValues;
    sResponse  = F("<html>\n<head>\n<title>");
    sResponse += Headertext;
    sResponse += F("</title>\n<script type=\"text/javascript\" src=\"https://www.google.com/jsapi?autoload={'modules':[{'name':'visualization','version':'1','packages':['gauge']}]}\"></script>\n<script type=\"text/javascript\">\nvar temp=");
    sResponse += pfTemp[iIndex];
    sResponse += F(",hum=");
    sResponse += pfHum[iIndex];
    if (pfPres[iIndex]<=900) {
      sResponse += F(",pres=1000");
    } else { 
      sResponse += F(",pres=");
      sResponse += pfPres[iIndex];
    }
    sResponse += F(";\ngoogle.load('visualization', '1', {packages: ['gauge']});google.setOnLoadCallback(drawgaugetemp);google.setOnLoadCallback(drawgaugehum);google.setOnLoadCallback(drawgaugepres);\nvar gaugetempOptions = {min: -20, max: 50, yellowFrom: -20, yellowTo: 0,redFrom: 30, redTo: 50, minorTicks: 10, majorTicks: ['-20','-10','0','10','20','30','40','50']};\n");
    sResponse += F("var gaugehumOptions = {min: 0, max: 100, yellowFrom: 0, yellowTo: 25, redFrom: 75, redTo: 100, minorTicks: 10, majorTicks: ['0','10','20','30','40','50','60','70','80','90','100']};\nvar gaugepresOptions = {min: 940, max: 1060, yellowFrom: 940, yellowTo: 960, redFrom: 1040, redTo: 1060, minorTicks: 10, majorTicks: ['940','960','980','1000','1020','1040','1060']};\nvar gaugetemp,gaugehum,gaugepres;\n\nfunction drawgaugetemp() {\ngaugetempData = new google.visualization.DataTable();\n");
    sResponse += F("gaugetempData.addColumn('number', '\260C');\ngaugetempData.addRows(1);\ngaugetempData.setCell(0, 0, temp);\ngaugetemp = new google.visualization.Gauge(document.getElementById('gaugetemp_div'));\ngaugetemp.draw(gaugetempData, gaugetempOptions);\n}\n\n");
    sResponse += F("function drawgaugehum() {\ngaugehumData = new google.visualization.DataTable();\ngaugehumData.addColumn('number', '%');\ngaugehumData.addRows(1);\ngaugehumData.setCell(0, 0, hum);\ngaugehum = new google.visualization.Gauge(document.getElementById('gaugehum_div'));\ngaugehum.draw(gaugehumData, gaugehumOptions);\n}\n");
    sResponse += F("function drawgaugepres() {\ngaugepresData = new google.visualization.DataTable();\ngaugepresData.addColumn('number', 'hPa');\ngaugepresData.addRows(1);\ngaugepresData.setCell(0, 0, pres);\ngaugepres = new google.visualization.Gauge(document.getElementById('gaugepres_div'));\ngaugepres.draw(gaugepresData, gaugepresOptions);\n}\n");
    sResponse += F("</script>\n</head>\n<body>\n<font color=\"#000000\"><body bgcolor=\"#d0d0f0\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\"><Font face=\"arial\"><h3>");
    sResponse  += Headertext;
    sResponse += F("</h3><FONT SIZE=+1>Stand ");
    sResponse += epoch_to_string(pulTime[iIndex]).c_str();
    sResponse += F(" UTC<BR>\n<div id=\"gaugetemp_div\" style=\"float:left; width:160px; height: 160px;\"></div> \n<div id=\"gaugehum_div\" style=\"float:left; width:160px; height: 160px;\"></div> \n<div id=\"gaugepres_div\" style=\"float:left; width:160px; height: 160px;\"></div>\n<div style=\"clear:both;\"></div>");
    
    sResponse2 = F("<p>");
    sResponse2 += F("<a href=\"/grafik1\">Grafik: Temperatur</a><br><a href=\"/grafik2\">Grafik: Feuchte</a><br><a href=\"/grafik3\">Grafik: Luftdruck</a><br><a href=\"/tabelle\">Tabelle</a></p></font>");
    sResponse2 += MakeHTTPFooter().c_str();
    
    // Send the response to the client 
    client.print(MakeHTTPHeader(sResponse.length()+sResponse2.length()).c_str());
    client.print(sResponse);
    client.print(sResponse2);
  }
  else if(sPath=="/tabelle")
  ////////////////////////////////////
  // format the html page for /tabelle
  ////////////////////////////////////
  {
    ulReqcount++;
    unsigned long ulSizeList = MakeTable(&client,false); // get size of table first
    
    sResponse  = F("<html><head><Font face=\"Arial\"><title>");
    sResponse += Headertext;
    sResponse += F("</title></head><body>");
    sResponse += F("<font color=\"#000000\"><body bgcolor=\"#d0d0f0\">");
    sResponse += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\"><h3>");
    sResponse += Headertext;
    sResponse += F("</h3>");
    sResponse += F("<FONT SIZE=+1>");
    sResponse += F("<a href=\"/\">Startseite</a><BR><BR>Mess-Intervall ");
    sResponse += ulMeasDelta_ms / 1000;
    sResponse += F(" Sekunden<BR>");
    // here the big table will follow later - but let us prepare the end first
      
    // part 2 of response - after the big table
    sResponse2 = MakeHTTPFooter().c_str();
    
    // Send the response to the client - delete strings after use to keep mem low
    client.print(MakeHTTPHeader(sResponse.length()+sResponse2.length()+ulSizeList).c_str()); 
    client.print(sResponse); sResponse="";
    MakeTable(&client,true);
    client.print(sResponse2);
  }
  else if(sPath=="/grafik1")
  ///////////////////////////////////
  // format the html page for /grafik1
  ///////////////////////////////////
  {
    ulReqcount++;
    unsigned long ulSizeList = MakeList(&client,false,1); // get size of list first

    sResponse  = F(" <html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">");
    sResponse += F("    <script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>");
    sResponse += F("    <script type=\"text/javascript\">");
    sResponse += F("      google.charts.load('current', {'packages':['corechart']});google.charts.setOnLoadCallback(drawChart);");
    sResponse += F("      function drawChart() {var data = google.visualization.arrayToDataTable([");
    sResponse += F("['Zeit / UTC', 'Temperatur'],");
   // here the big list will follow later - but let us prepare the end first
      
    // part 2 of response - after the big list
    sResponse2  = F("        ]);");
    sResponse2 += F("    var options = {title: 'Temperatur',vAxes:{0:{viewWindowMode:'explicit',gridlines:{color:'black'},format:\"##.#\260C\"},},");
    sResponse2 += F("     series:{0:{targetAxisIndex:0},},curveType:'none',legend:{ position: 'bottom'}};");
    sResponse2 += F("        var chart = new google.visualization.LineChart(document.getElementById('curve_chart')); chart.draw(data, options);");
    sResponse2 += F("      }    </script>  </head> <body>   <div id=\"curve_chart\" style=\"width: 900px; height: 500px\"></div>");
    sResponse2 += MakeHTTPFooter().c_str();
    
    // Send the response to the client - delete strings after use to keep mem low
    client.print(MakeHTTPHeader(sResponse.length()+sResponse2.length()+ulSizeList).c_str()); 
    client.print(sResponse); sResponse="";
    MakeList(&client,true,1);
    client.print(sResponse2); sResponse2="";
  }
  else if(sPath=="/grafik2")
  ///////////////////////////////////
  // format the html page for /grafik
  // Feuchtigkeit
  ///////////////////////////////////
  {
    ulReqcount++;
    unsigned long ulSizeList = MakeList(&client,false,2); // get size of list first

    sResponse  = F(" <html>  <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">");
    sResponse += F("    <script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>");
    sResponse += F("    <script type=\"text/javascript\">");
    sResponse += F("      google.charts.load('current', {'packages':['corechart']});google.charts.setOnLoadCallback(drawChart);");
    sResponse += F("      function drawChart() {var data = google.visualization.arrayToDataTable([");
    sResponse += F("['Zeit / UTC', 'Feuchte'],");

   // here the big list will follow later - but let us prepare the end first
      
    // part 2 of response - after the big list
    sResponse2  = F("        ]);");
    sResponse2 += F("    var options = {title: 'Feuchte',vAxes:{0:{viewWindowMode:'explicit',gridlines:{color:'black'},format:\"##,## %\"},},");
    sResponse2 += F("     series:{0:{targetAxisIndex:0},},curveType:'none',legend:{ position: 'bottom'}};");
    sResponse2 += F("        var chart = new google.visualization.LineChart(document.getElementById('curve_chart')); chart.draw(data, options);");
    sResponse2 += F("      }   </script>  </head>  <body>    <div id=\"curve_chart\" style=\"width: 900px; height: 500px\"></div>");
    sResponse2 += MakeHTTPFooter().c_str();

    // Send the response to the client - delete strings after use to keep mem low
    client.print(MakeHTTPHeader(sResponse.length()+sResponse2.length()+ulSizeList).c_str()); 
    client.print(sResponse); sResponse="";
    MakeList(&client,true,2);
    client.print(sResponse2); sResponse2="";
  }
  else if(sPath=="/grafik3")
  ///////////////////////////////////
  // format the html page for /grafik
  // Luftdruck
  ///////////////////////////////////
  {
    ulReqcount++;
    unsigned long ulSizeList = MakeList(&client,false,3); // get size of list first

    sResponse  = F(" <html>  <head>    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">");
    sResponse += F("    <script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>");
    sResponse += F("    <script type=\"text/javascript\">");
    sResponse += F("      google.charts.load('current', {'packages':['corechart']});google.charts.setOnLoadCallback(drawChart);");
    sResponse += F("      function drawChart() {var data = google.visualization.arrayToDataTable([");
    sResponse += F("['Zeit / UTC', 'Luftdruck'],");

   // here the big list will follow later - but let us prepare the end first
      
    // part 2 of response - after the big list
    sResponse2  = F("        ]);");
    sResponse2 += F("    var options = {title: 'Luftdruck',vAxes:{0:{viewWindowMode:'explicit',gridlines:{color:'black'},format:\"####.# hPa\"},},");
    sResponse2 += F("     series:{0:{targetAxisIndex:0},},curveType:'none',legend:{ position: 'bottom'}};");
    sResponse2 += F("        var chart = new google.visualization.LineChart(document.getElementById('curve_chart'));        chart.draw(data, options);");
    sResponse2 += F("      }    </script>  </head>  <body>    <div id=\"curve_chart\" style=\"width: 900px; height: 500px\"></div>");
    sResponse2 += MakeHTTPFooter().c_str();

    // Send the response to the client - delete strings after use to keep mem low
    client.print(MakeHTTPHeader(sResponse.length()+sResponse2.length()+ulSizeList).c_str()); 
    client.print(sResponse); sResponse="";
    MakeList(&client,true,3);
    client.print(sResponse2); sResponse2="";
  }
  else
  ////////////////////////////
  // 404 for non-matching path
  ////////////////////////////
  {
    sResponse="<html><head><title>404 Not Found</title></head><body><h1>Was ein Schmarrn rufst du denn auf?</h1><p>Des hoab i hoalt net.</p></body></html>";
    
    sHeader  = F("HTTP/1.1 404 Not found\r\nContent-Length: ");
    sHeader += sResponse.length();
    sHeader += F("\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
    
    // Send the response to the client
    client.print(sHeader);
    client.print(sResponse);
  }
  
  // and stop the client
  client.stop();
  Serial.println("Client disconnected");
}
