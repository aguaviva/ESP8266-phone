package com.example.aguaviva.myphone;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;

public class MainActivity extends Activity {

    private Button button;
    private TextView textView;
    private EditText url;
    private ImageView surface;
    private SurfaceHolder surfaceHolder;
    private Connect connect;
    private CheckBox checkBoxPlay, checkBoxRec;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        SurfaceView surface = (SurfaceView) findViewById(R.id.surfaceView);
        surfaceHolder = surface.getHolder();

        textView = (TextView) findViewById(R.id.textView);
        textView.setMovementMethod(new ScrollingMovementMethod());

        url = (EditText) findViewById(R.id.editText);
        button = (Button) findViewById(R.id.button);
        checkBoxPlay = (CheckBox) findViewById(R.id.checkBoxPlay);
        checkBoxRec = (CheckBox) findViewById(R.id.checkBoxRec);


        connect = new Connect(this);

        // Capture button clicks
        button.setOnClickListener(new View.OnClickListener() {
            public void onClick(View arg0) {
                connect.OnButton();
            }
        });
    }

    @Override
    protected void onDestroy()
    {
        connect.interrupt();
    }

    public boolean GetRecordingState() { return checkBoxRec.isChecked();}
    public boolean GetPlayingState() { return checkBoxPlay.isChecked();}
    public String GetHostState() { return url.getText().toString();}

    public void AddMsg(String st)
    {
        final String str = st;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                textView.setText(textView.getText() + str);
            }
        });
    }

    public void OnConnect(boolean _bIsConnected)
    {
        final boolean bIsConnected = _bIsConnected;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                //Your code to run in GUI thread here
                if (bIsConnected) {
                    button.setText("Disconnect");
                    checkBoxPlay.setEnabled(false);
                    checkBoxRec.setEnabled(false);
                }
                else
                {
                    button.setText("Connect");
                    checkBoxPlay.setEnabled(true);
                    checkBoxRec.setEnabled(true);
                }
            }
        });
    }


    float xx = 0;
    public void DrawWave(float value) {

        Canvas canvas = surfaceHolder.lockCanvas();
        if (canvas != null)
        {
            Paint mPaint = new Paint();
            mPaint.setColor(Color.GREEN);
            mPaint.setStrokeWidth(2);
            float cy = (canvas.getHeight()/2);
            value += cy;
            canvas.drawLine(xx,cy,xx, value, mPaint);
            xx+=1;
            if (xx>=canvas.getWidth())
                xx=0;

            surfaceHolder.unlockCanvasAndPost(canvas);
        }
    }

    @Override
    public void onSaveInstanceState(Bundle savedInstanceState) {
        savedInstanceState.putString("url", url.getText().toString());
        savedInstanceState.putCharSequence("textView", textView.getText());
        savedInstanceState.putCharSequence("button", button.getText());
        savedInstanceState.putBoolean("checkBoxPlay", checkBoxPlay.isChecked());
        savedInstanceState.putBoolean("checkBoxRec", checkBoxRec.isChecked());

        super.onSaveInstanceState(savedInstanceState);
    }

    @Override
    public void onRestoreInstanceState(Bundle savedInstanceState) {
        super.onRestoreInstanceState(savedInstanceState);

        url.setText(savedInstanceState.getCharSequence("url"));
        textView.setText(savedInstanceState.getCharSequence("textView"));
        button.setText(savedInstanceState.getCharSequence("button"));
        checkBoxPlay.setChecked(savedInstanceState.getBoolean("checkBoxPlay"));
        checkBoxRec.setChecked(savedInstanceState.getBoolean("checkBoxRec"));
    }

}
