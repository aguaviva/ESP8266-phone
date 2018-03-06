package com.example.aguaviva.myphone;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.NoiseSuppressor;
import android.os.Build;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;

/**
 * Created by raguaviv on 1/9/2018.
 */

public class Worker extends Thread {

    private boolean isPlayer;
    private boolean isRecorder;
    private OutputStream os;
    private InputStream is;
    public static int BUFFER_SIZE = (160*2)*3;
    private byte[] recording_buffer = new byte[BUFFER_SIZE];
    private byte[] playing_buffer = new byte[BUFFER_SIZE];
    private Connect connect;

    public Worker(Connect connect)
    {
        this.connect = connect;
    }

    public boolean init(Socket socket, boolean isPlayer, boolean isRecorder)
    {
        this.isPlayer = isPlayer;
        this.isRecorder = isRecorder;

        this.socket = socket;
        try
        {
            os = socket.getOutputStream();
            is = socket.getInputStream();
        }
        catch (Exception e)
        {
            return false;
        }

        // Open AudioRecord
        if (isRecorder) {
            int min_in = AudioRecord.getMinBufferSize(8000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            min_in = Math.max(recording_buffer.length, min_in);
            connect.AddMsg("Recording in: " + min_in + "\n");
            record = new AudioRecord(MediaRecorder.AudioSource.MIC, 8000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, min_in);

            if (record==null)
                return false;

            if (Build.VERSION.SDK_INT >= 16) {
                if (AcousticEchoCanceler.isAvailable()) {
                    AcousticEchoCanceler aec = AcousticEchoCanceler.create(record.getAudioSessionId());
                    if (aec != null && !aec.getEnabled()) {
                        aec.setEnabled(true);
                    }
                }
                if (NoiseSuppressor.isAvailable()) {
                    NoiseSuppressor noise = NoiseSuppressor.create(record.getAudioSessionId());
                    if (noise != null && !noise.getEnabled()) {
                        noise.setEnabled(true);
                    }
                }
            }
        }

        if (isPlayer) {
            int min_out = AudioTrack.getMinBufferSize(8000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
            min_out = Math.max(playing_buffer.length,min_out);
            connect.AddMsg("Playing out: "+min_out+"\n");

            play = new AudioTrack(AudioManager.STREAM_MUSIC, 8000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT, min_out, AudioTrack.MODE_STREAM);
            if (play==null)
                return false;
        }

        try {
            if (isRecorder) {
                record.startRecording();
            }
            if (isPlayer) {
                play.play();
            }
        }
        catch(Exception e)
        {
            connect.AddMsg("Could not init audio, have you granted recording permissions?\n");
            return false;
        }

        return true;
    }

    @Override
    public void run()
    {
        super.run();

        int frames = 0;
        running = true;
        long lastTime = System.currentTimeMillis();

        // Loop
        try {
            while (!interrupted() && running) {

                if (isRecorder) {
                    Integer recorded_bytes = record.read(recording_buffer, 0, recording_buffer.length);
                    os.write(recording_buffer, 0, recorded_bytes);
                    //connect.DrawWave(-recorded_bytes.floatValue());
                }

                if (isPlayer) {
                    Integer read_bytes = is.read(playing_buffer, 0, playing_buffer.length);
                    if (read_bytes<0) {
                        break;
                    }
                    play.write(playing_buffer, 0, read_bytes);
                    //activity.DrawWave(read_data.floatValue());
                }

                frames ++;

                if (frames >= 8000 / (160*3)) {
                    frames = 0;
                    Long currTime = System.currentTimeMillis();
                    Long deltaTime = currTime - lastTime;
                    lastTime = currTime;

                    if (isRecorder)
                        connect.AddMsg("Load rec: " + deltaTime + "!\n");
                    if (isPlayer)
                        connect.AddMsg("Load ply: " + deltaTime + "!\n");
                }
            };
        }
        catch (IOException e)
        {
            if (!interrupted()) {
                connect.AddMsg(e.getMessage() + "\n");
            }
            connect.OnDisconnected();
            interrupt();
        }

        running = false;

        if(record != null)
        {
            record.stop();
            record.release();
            record = null;
            connect.AddMsg("recording stopped!\n");
        }

        if (play!=null) {
            play.stop();
            play.release();
            connect.AddMsg("playing stopped!\n");
        }
    }

    // MARK: Private Method

    private Socket socket;
    private AudioRecord record;
    private AudioTrack play;

    // MARK: Public Method

    private boolean running = false;
    public boolean isRunning() {
        return this.running;
    }
}
