# ESP8266 phone

Still work in progress.


- Recording in the ESP8266 and listening in the browser is working.
- Recording in the Browser and listening in the ESP8266 is working.
- openlpc is fully working, it can 
  - decode hola.lpc and
    - send the data over http ( /decode.raw )
    - play the audio using i2s  ( /PlayHolaLPC )
  - encode hola.raw and
    - send the data over http ( /encode.lpc )
    - record from the mic and send it via socket ( /index.html )

notes:
- the I2S needs this pull request https://github.com/esp8266/Arduino/pull/3995
- you can play hola.raw using
  - /PlayHolaRaw (data is sent to the i2s byte by byte)
  - /PlayHolaRawBuf (data is sent to the i2s in chucks, more efficient!)
- I measured the sampling rate of the I2S and turned to be slower than 8Khz, this causes dropped packets.
- recording and playing still doesn't work.
