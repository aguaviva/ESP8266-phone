package com.example.aguaviva.myphone;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.util.Enumeration;


/**
 * Created by raguaviv on 1/11/2018.
 */

public class Connect extends Thread {

    enum State
    {
        waitingForCall,
        tryingToConnect,
        disconnected, connected
    }

    private String host;
    private Integer port=9999;
    public boolean bDoPlay=true, bDoRec=true;

    private ServerSocket serverSocket;

    private MainActivity activity;

    private Worker workerPlayer;
    private Worker workerRecorder;

    private Object mPauseLock = new Object();
    State state;

    public Connect(MainActivity activity)
    {
        state = State.waitingForCall;
        this.activity = activity;
        start();
    }

    public InetAddress GetMyIP()
    {
        try {
            Enumeration<NetworkInterface> enumNetworkInterfaces = NetworkInterface.getNetworkInterfaces();
            while (enumNetworkInterfaces.hasMoreElements()) {
                NetworkInterface networkInterface = enumNetworkInterfaces.nextElement();
                Enumeration<InetAddress> enumInetAddress = networkInterface.getInetAddresses();
                while (enumInetAddress.hasMoreElements()) {
                    InetAddress inetAddress = enumInetAddress.nextElement();

                    if (inetAddress.isSiteLocalAddress()) {
                        return inetAddress;
                    }
                }
            }
        } catch (SocketException e) {
            // TODO Auto-generated catch block
            activity.AddMsg("not listening\n");
        }

        return null;
    }


    public void GetHostPort()
    {
        //get ip and port
        String[] bits = activity.GetHostState().split(":", 2);
        if (bits.length != 2) {
            AddMsg("Need <ip>:<port>\n");
        } else {
            this.host = bits[0];
            this.port = Integer.parseInt(bits[1]);
        }
    }

    Socket Connect()
    {
        try
        {
            GetHostPort();
            activity.AddMsg("connecting to: " + host + " : " + port + " \n");
            Socket socket = new Socket();
            socket.connect(new InetSocketAddress(host , port),3000);
            socket.setSoTimeout(1000);
            return socket;
        }
        catch (Exception e)
        {
            AddMsg(e.getMessage()+"\n");
        }
        return null;
    }

    void notifyLoop() {
        synchronized (mPauseLock) {
            mPauseLock.notifyAll();
        }
    }

    void WaitLoop() {
        synchronized (mPauseLock) {
            try {
                mPauseLock.wait();
            } catch (InterruptedException e) {
                interrupt(); // in this case the thread was interrupted while in the wait
            }
        }
    }

    void StopListening()
    {
        try
        {
            if (serverSocket!=null) {
                serverSocket.close();
                serverSocket = null;
            }
        }
        catch (Exception e)
        {
            AddMsg(e.getMessage()+"\n");
        }
    }

    Socket Listen()
    {
        try {
            GetHostPort();
            activity.AddMsg("listening at "+ GetMyIP().getHostAddress()+":"+port+"\n");
            serverSocket = new ServerSocket(port);
            Socket socket = serverSocket.accept();
            serverSocket.close();
            serverSocket=null;
            socket.setSoTimeout(1000);
            return socket;
        } catch (IOException e) {
            // TODO Auto-generated catch block
            AddMsg(e.getMessage()+"\n");
        }
        return null;
    }

    public void OnButton()
    {
        switch(state)
        {
            case waitingForCall:
                state = State.tryingToConnect;
                StopListening();
                break;

            case connected: notifyLoop(); break;
            case tryingToConnect: break;
        }
    }

    public void OnDisconnected() {
        notifyLoop();
    }

    public void AddMsg(String str)
    {
        activity.AddMsg(str);
    }

    @Override
    public void run() {

        Socket socket = null;

        while (!interrupted())
        {
            if (state == State.waitingForCall) {
                socket = Listen();
            } else if (state == State.tryingToConnect) {
                socket = Connect();
            }

            if (socket!=null && socket.isConnected()) {
                activity.AddMsg("connected\n");
                activity.OnConnect(true);
                state = State.connected;

                workerPlayer = new Worker(this);
                workerRecorder = new Worker(this);

                if (workerRecorder.init(socket, false, true)) {
                    workerPlayer.init(socket, true, false);

                    if (activity.GetPlayingState())
                        workerPlayer.start();
                    if (activity.GetRecordingState())
                        workerRecorder.start();

                    WaitLoop();

                    try {
                        socket.close();
                        socket = null;
                    } catch (IOException e) {
                        AddMsg(e.getMessage() + "\n");
                    }

                    try {
                        workerPlayer.interrupt();
                        workerRecorder.interrupt();
                        workerPlayer.join();
                        workerRecorder.join();
                    } catch (Exception e) {
                        AddMsg(e.getMessage() + "\n");
                    }
                }
                else
                {
                    try {
                        socket.close();
                        socket = null;
                    } catch (IOException e) {
                        AddMsg(e.getMessage() + "\n");
                    }
                }

                AddMsg("disconnected\n");
                activity.OnConnect(false);
                state = State.waitingForCall;
            }
            else
            {
                try {
                    socket.close();
                    socket = null;
                } catch (Exception e) {
                    AddMsg(e.getMessage() + "\n");
                }

                state = State.waitingForCall;
            }
        }
    }
}
