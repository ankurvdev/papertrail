package com.zzzsAndroid.AppPackageNamezzze

import android.Manifest
import android.annotation.SuppressLint
import android.app.DownloadManager
import android.content.*
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.*
import android.webkit.URLUtil
import android.webkit.WebView
import androidx.annotation.Keep
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.google.android.gms.location.*


fun PackageManager.getPackageInfoCompat(packageName: String, flags: Int = 0): PackageInfo =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        getPackageInfo(packageName, PackageManager.PackageInfoFlags.of(flags.toLong()))
    } else {
        @Suppress("DEPRECATION") getPackageInfo(packageName, flags)
    }

class MainActivity : AppCompatActivity() {


    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        instance = this
        System.loadLibrary("native_glue")
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        mWebView = findViewById(R.id.webview)
        mWebView.settings.javaScriptEnabled = true
        mWebView.setDownloadListener { url, userAgent, contentDescription, mimetype, _ ->

            // Initialize download request
            val request: DownloadManager.Request = DownloadManager.Request(Uri.parse(url))
            // Add the download request header
            request.addRequestHeader("User-Agent", userAgent)

            // Set download request description
            request.setDescription("Downloading requested file....")

            // Set download request mime type
            request.setMimeType(mimetype)

            // Download request notification setting
            request.setNotificationVisibility(
                DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED
            )

            // Guess the file name
            val fileName: String = URLUtil.guessFileName(url, contentDescription, mimetype)

            // Set a destination storage for downloaded file
            request.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, fileName)

            // Set request title
            request.setTitle(URLUtil.guessFileName(url, contentDescription, mimetype))


            // DownloadManager request more settings
            request.setAllowedOverMetered(true)
            request.setAllowedOverRoaming(false)
            // Get the system download service
            val dManager: DownloadManager =
                getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager

            // Finally, request the download to system download service
            dManager.enqueue(request)
        }

        billingManager = BillingManager(this)
        // Bind to LocalService
        locationServiceIntent = Intent(this, LocationForegroundService::class.java).also { intent ->
            bindService(intent, locationServiceConnection, Context.BIND_AUTO_CREATE)
        }

        locationServiceForegroundIntent =
            Intent(this, LocationForegroundService::class.java).putExtra("Foreground", true)
        glueAppInit()

    }
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {

        }
    }

    override fun onResume() {
        // States
        // 1. Service Running in the background
        // 2. Pending Permissions
        //
        super.onResume()
        glueAppActivityStart()
        mWebView.loadUrl(glueGetWebserverUrl())
    }


    private fun onRequestIAPInit(iapList: String) {
        billingManager.init(this, iapList.split(",").toTypedArray())
    }

    private fun tryRequestPermissions(requests: Array<String>, requestCode: Int) {
        if (pendingCodes.size == 0) {
            pendingCodes.add(requestCode)
            ActivityCompat.requestPermissions(
                this@MainActivity, requests, requestCode
            )
        } else {
            pendingCodes.add(requestCode)
        }
    }

    private fun ensureLocationPermissions(requestCode: Int): Boolean {
        val packageInfo =
            packageManager.getPackageInfoCompat(packageName, PackageManager.GET_PERMISSIONS)
        val permissionList = listOf(*packageInfo.requestedPermissions)
        val needFine = ActivityCompat.checkSelfPermission(
            this, Manifest.permission.ACCESS_FINE_LOCATION
        ) != PackageManager.PERMISSION_GRANTED
        val needCoarse = ActivityCompat.checkSelfPermission(
            this, Manifest.permission.ACCESS_COARSE_LOCATION
        ) != PackageManager.PERMISSION_GRANTED
        val needBackground =
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && permissionList.contains(Manifest.permission.ACCESS_BACKGROUND_LOCATION) && ActivityCompat.checkSelfPermission(
                this, Manifest.permission.ACCESS_BACKGROUND_LOCATION
            ) != PackageManager.PERMISSION_GRANTED

        if (needFine || needCoarse) {
            if (!locationPermissionDenied) {
                tryRequestPermissions(
                    arrayOf(
                        Manifest.permission.ACCESS_FINE_LOCATION,
                        Manifest.permission.ACCESS_COARSE_LOCATION
                    ), requestCode
                )
            }
            return false
        } else if (needBackground && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            if (!locationBackgroundPermissionDenied) {
                tryRequestPermissions(
                    arrayOf(Manifest.permission.ACCESS_BACKGROUND_LOCATION), requestCode
                )
                return false
            }
            return true
        } else {
            return true
        }

    }

    private fun ensureForegroundPermissions(): Boolean {
        if (ActivityCompat.checkSelfPermission(
                this, Manifest.permission.POST_NOTIFICATIONS
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            if (!notificationPermissionDenied) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    tryRequestPermissions(
                        arrayOf(Manifest.permission.POST_NOTIFICATIONS), 35
                    )
                }

            }
            return false
        }
        return true
    }

    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<String>, grantResults: IntArray
    ) {
        // Dequeue request
        if (grantResults.isNotEmpty() && permissions[0] == Manifest.permission.ACCESS_FINE_LOCATION && grantResults[0] == PackageManager.PERMISSION_DENIED) {
            locationPermissionDenied = true
        }
        if (grantResults.isNotEmpty() && permissions[0] == Manifest.permission.POST_NOTIFICATIONS && grantResults[0] == PackageManager.PERMISSION_DENIED) {
            notificationPermissionDenied = true
        }
        if (grantResults.isNotEmpty() && permissions[0] == Manifest.permission.ACCESS_BACKGROUND_LOCATION && grantResults[0] == PackageManager.PERMISSION_DENIED) {
            locationBackgroundPermissionDenied = true
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val oldPendingCodes = pendingCodes
        pendingCodes = emptyList<Int>().toMutableSet()
        for (code in oldPendingCodes) {
            if (code == 34) {
                onStartLocationUpdate()
            }
            if (code == 35) {
                onStartForegroundLocationUpdate()
            }
        }
    }

    private fun onStartLocationUpdate() {
        // When app needs location
        if (!ensureLocationPermissions(34)) {
            return
        }
        startService(locationServiceIntent)
    }


    private fun onStartForegroundLocationUpdate() {
        // App needs location in the background
        if (!ensureLocationPermissions(35)) {
            return
        }
        if (!ensureForegroundPermissions()) {
            return
        }
        startForegroundService(locationServiceForegroundIntent)
    }

    private fun onStopLocationUpdate() {
        // When the app no longer needs location
        stopService(locationServiceIntent)
        if (locationService != null) unbindService(locationServiceConnection)
        locationService = null
    }

    private fun onStopForegroundLocationUpdate() {
        // When the app no longer needs location in background
        locationService?.relinquishForeground()
    }

    private fun onStartOrientationUpdate() {
        updateOrientation = true
    }

    private fun onStopOrientationUpdate() {
        updateOrientation = false
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        var instance: MainActivity? = null

        @JvmStatic
        @Keep
        private fun cbStartLocationUpdate() {
            instance?.onStartLocationUpdate()
        }

        @JvmStatic
        @Keep
        private fun cbStartForegroundLocationUpdate() {
            instance?.onStartForegroundLocationUpdate()
        }

        @JvmStatic
        @Keep
        private fun cbStopLocationUpdate() {
            instance?.onStopLocationUpdate()
        }

        @JvmStatic
        @Keep
        private fun cbStopForegroundLocationUpdate() {
            instance?.onStopForegroundLocationUpdate()
        }

        @JvmStatic
        @Keep
        private fun cbStartOrientationUpdate() {
            instance?.onStartOrientationUpdate()
        }

        @JvmStatic
        @Keep
        private fun cbStopOrientationUpdate() {
            instance?.onStopOrientationUpdate()
        }

        @JvmStatic
        @Keep
        private fun cbRequestIAPInit(iapList: String) {
            instance?.onRequestIAPInit(iapList)
        }

        @JvmStatic
        @Keep
        private fun cbRequestIAPFeature(productToPurchase: String) {
            instance?.onRequestIAPFeature(productToPurchase)
        }

    }

    override fun onPause() {
        super.onPause()
        billingManager.shutdown()
        mWebView.loadUrl("about:blank")
        glueAppActivityStop()
    }

    override fun onDestroy() {
        super.onDestroy()
        instance = null
    }

    override fun onConfigurationChanged(configuration: Configuration) {
        super.onConfigurationChanged(configuration)
        if (updateOrientation) {
            glueUpdateOrientation(configuration.orientation)
        }
    }

    private fun onRequestIAPFeature(productToPurchase: String) {
        billingManager.initiatePurchase(productToPurchase)
    }

    private var locationService: LocationForegroundService? = null
    private lateinit var locationServiceIntent: Intent
    private lateinit var locationServiceForegroundIntent: Intent

    /** Defines callbacks for service binding, passed to bindService()  */
    private val locationServiceConnection = object : ServiceConnection {
        override fun onServiceConnected(className: ComponentName, service: IBinder) {
            // We've bound to LocalService, cast the IBinder and get LocalService instance
            val binder = service as LocationForegroundService.LocalBinder
            locationService = binder.getService()
        }

        override fun onServiceDisconnected(arg0: ComponentName) {
            locationService = null
        }
    }

    private var pendingCodes: MutableSet<Int> = emptyList<Int>().toMutableSet()
    private var locationPermissionDenied = false
    private var locationBackgroundPermissionDenied = false
    private var notificationPermissionDenied = false
    private var updateOrientation = false

    private lateinit var billingManager: BillingManager
    private lateinit var mWebView: WebView


    private external fun glueAppInit()
    private external fun glueAppActivityStart()
    private external fun glueAppActivityStop()
    private external fun glueGetWebserverUrl(): String
    private external fun glueUpdateOrientation(orientation: Int)
}
