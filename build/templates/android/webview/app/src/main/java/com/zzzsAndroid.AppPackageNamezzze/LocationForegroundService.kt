package com.zzzsAndroid.AppPackageNamezzze

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.location.Location
import android.os.Binder
import android.os.IBinder
import android.os.Looper
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import com.google.android.gms.location.*

/* Tests
Interesting Points
1. Main-Activity start without Foreground (Launch)
2. Main-Activity stop without Foreground (Launch + Home + Service-Close)// Always happens
3. Main-Activity pause without Foreground (Launch + Service-Close + Home)
4. Main-Activity resume without Foreground (Launch + Service-Close + Home + Launch)
7. Main-Activity pause with Foreground (Launch +  Home)
8. Main-Activity resume with Foreground (Launch +  Home + Launch)

5. No Main-Activity start with Foreground (
6. No Main-Activity stop with Foreground

Launch + Home + Service-Close + Launch
1, 7, 2, 4
Launch + Service-Close + Home
1, 3, 2,
Launch + Home + Launch
1, 7, 8, 6

 */
class LocationForegroundService : Service() {
    private var isIdle = false

    inner class LocalBinder : Binder() {
        // Return this instance of LocalService so clients can call public methods
        fun getService(): LocationForegroundService = this@LocationForegroundService
    }

    private var mFusedLocationProviderClient: FusedLocationProviderClient? = null

    private val binder = LocalBinder()

    override fun onBind(intent: Intent): IBinder {
        return binder
    }

    override fun onUnbind(intent: Intent?): Boolean {
        stopService(intent)
        return false
    }

    private fun onStartLocationUpdate() {
        if (ActivityCompat.checkSelfPermission(
                this,
                Manifest.permission.ACCESS_FINE_LOCATION
            ) != PackageManager.PERMISSION_GRANTED && ActivityCompat.checkSelfPermission(
                this,
                Manifest.permission.ACCESS_COARSE_LOCATION
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            return
        }
        isIdle = false
        if (mFusedLocationProviderClient == null) {
            mFusedLocationProviderClient = LocationServices.getFusedLocationProviderClient(this)
            val locationRequest = LocationRequest.Builder(Priority.PRIORITY_HIGH_ACCURACY, 250).build()
            val locationCallback = object : LocationCallback() {
                override fun onLocationResult(locationResult: LocationResult) {
                    super.onLocationResult(locationResult)
                    locationResult.lastLocation?.let { glueUpdateLocation(it) }
                }
            }
            mFusedLocationProviderClient!!.requestLocationUpdates(
                locationRequest, locationCallback as LocationCallback, Looper.getMainLooper()
            )
        }
    }

    private fun onStartServiceInForeground() {
        val pendingIntent: PendingIntent =
            Intent(this, MainActivity::class.java).let { notificationIntent ->
                PendingIntent.getActivity(this, 0, notificationIntent, PendingIntent.FLAG_IMMUTABLE)
            }

        val serviceActionIntent: PendingIntent = Intent(this, LocationForegroundService::class.java).let {
            it.putExtra("Close", true)
            PendingIntent.getService(this, 0, it, PendingIntent.FLAG_IMMUTABLE)
        }

        val manager = (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)

        manager.createNotificationChannel(
            NotificationChannel(
                "channel.zzzsAndroid.AppPackageNamezzze", // Notification channel Id
                "Background Service",
                NotificationManager.IMPORTANCE_MIN
            )
        )

        val notificationBuilder =
            NotificationCompat.Builder(this, "channel.zzzsAndroid.AppPackageNamezzze")
        val notification = notificationBuilder
            .setOngoing(true)
            .setContentTitle("Running in background")
            // .setContentText("Test")
            // this is important, otherwise the notification will show the way
            // you want i.e. it will show some default notification
            .setSmallIcon(R.mipmap.ic_launcher)
            .setPriority(NotificationManager.IMPORTANCE_MIN)
            .setContentIntent(pendingIntent)
            .setStyle(NotificationCompat.BigTextStyle())
            .addAction(R.mipmap.ic_launcher, "Close", serviceActionIntent)
            .build()
        startForeground(2, notification)
        glueAppLocationServiceStarted()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent != null && intent.getBooleanExtra("Close", false)) {
            relinquishForeground()
        } else {
            onStartLocationUpdate()
            if (intent != null && intent.getBooleanExtra("Foreground", false)) {
                onStartServiceInForeground()
            }
        }
        return START_NOT_STICKY
    }

    fun relinquishForeground() {
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        glueAppLocationServiceStopped()

    }

    private external fun glueAppLocationServiceStopped()
    private external fun glueAppLocationServiceStarted()

    private external fun glueUpdateLocation(location: Location)

}