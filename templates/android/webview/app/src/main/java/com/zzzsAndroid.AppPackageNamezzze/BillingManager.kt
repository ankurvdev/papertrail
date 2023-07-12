package com.zzzsAndroid.AppPackageNamezzze

import android.app.Activity
import android.content.Context
import android.util.Log
import com.android.billingclient.api.*


class BillingManager constructor(ctx: Context) :
    AcknowledgePurchaseResponseListener,
    BillingClientStateListener,
    PurchasesResponseListener,
    PurchasesUpdatedListener,
    ProductDetailsResponseListener {

    private var availableProductDetails = HashMap<String, ProductDetails>()

    private var billingClient = BillingClient.newBuilder(ctx)
        .setListener(this)
        .enablePendingPurchases() // Not used for subscriptions, but required.
        .build()

    private var state = 0
    private lateinit var activity: Activity
    private lateinit var knownProducts: Array<String>

    // Success
    private external fun glueIAPInitialized()
    private external fun glueIAPRequestFailed(name: String, code: Int, message: String)
    private external fun glueIAPAvailable(name: String)
    private external fun glueIAPReset()

    fun init(activity1: Activity, knownProducts1: Array<String>) {
        if (state == 1 || state == 2) return
        billingClient.startConnection(this)
        state = 1 /* Initializing */
        activity = activity1
        knownProducts = knownProducts1
    }

    fun shutdown() {
        glueIAPReset()
        billingClient.endConnection()
        state = 0
    }

    fun initiatePurchase(productToPurchase: String) {
        if (state != 2) {
            glueIAPRequestFailed(productToPurchase, -2, "Initialization not completed $productToPurchase")
        }

        val productDetails = availableProductDetails[productToPurchase]
        if (productDetails == null) {
            glueIAPRequestFailed(productToPurchase, -3, "Cannot Find Sku for given product: $productToPurchase")
            return
        }

        val billingParams = BillingFlowParams
            .newBuilder()
            .setProductDetailsParamsList(
                listOf(
                    BillingFlowParams.ProductDetailsParams.newBuilder()
                        .setProductDetails(productDetails)
                        .build()
                )
            )
            .build()
        val billingResult = billingClient.launchBillingFlow(activity, billingParams)
        if (billingResult.responseCode != BillingClient.BillingResponseCode.OK) {
            glueIAPRequestFailed(productToPurchase, billingResult.responseCode, "Launch Billing Flow Failed")
        }
    }

    override fun onBillingSetupFinished(billingResult: BillingResult) {
        if (billingResult.responseCode == BillingClient.BillingResponseCode.OK) {
            if (knownProducts.isNotEmpty()) {
                val productList: ArrayList<QueryProductDetailsParams.Product> = ArrayList()
                for (product in knownProducts) {
                    val parts = product.split(':')
                    val productName = parts.last()
                    var productType = BillingClient.ProductType.INAPP
                    if (parts.size > 1) {
                        productType = if (parts[0].lowercase() == "sub") {
                            BillingClient.ProductType.SUBS
                        } else if (parts[0].lowercase() == "inapp") {
                            BillingClient.ProductType.INAPP
                        } else {
                            Log.w("zzzsAndroid.AppPackageNamezzze", "Ignoring invalid product :$product")
                            continue
                        }
                    }

                    productList.add(
                        QueryProductDetailsParams.Product.newBuilder()
                            .setProductId(productName)
                            .setProductType(productType)
                            .build()
                    )
                }

                val params = QueryProductDetailsParams.newBuilder().setProductList(productList)
                billingClient.queryProductDetailsAsync(params.build(), this)
            }

            billingClient.queryPurchasesAsync(
                QueryPurchasesParams.newBuilder()
                    .setProductType(BillingClient.ProductType.INAPP).build(), this
            )

        } else {
            state = -1
        }
    }

    override fun onBillingServiceDisconnected() {
        shutdown()
    }

    override fun onProductDetailsResponse(billingResult: BillingResult, productDetailsList: MutableList<ProductDetails>) {
        if (billingResult.responseCode != BillingClient.BillingResponseCode.OK) {
            state = -1
            return
        }

        for (details in productDetailsList) {
            availableProductDetails[details.productId] = details
        }
        state = 2
        glueIAPInitialized()
    }

    private fun processPurchases(billingResult: BillingResult, purchases: MutableList<Purchase>) {
        if (billingResult.responseCode == BillingClient.BillingResponseCode.OK) {
            for (purchase in purchases) {
                if (!purchase.isAcknowledged) {
                    val acknowledgePurchaseParams = AcknowledgePurchaseParams.newBuilder()
                        .setPurchaseToken(purchase.purchaseToken)
                        .build()
                    billingClient.acknowledgePurchase(acknowledgePurchaseParams, this)
                } else {
                    for (product in purchase.products) {
                        glueIAPAvailable(product)
                    }
                }
            }
        }
        for (purchase in purchases) {
            for (sku in purchase.products) {
                glueIAPRequestFailed(sku, billingResult.responseCode, "Purchase Update Failed")
            }
        }
    }

    override fun onQueryPurchasesResponse(billingResult: BillingResult, purchases: MutableList<Purchase>) {
        processPurchases(billingResult, purchases)
    }

    override fun onPurchasesUpdated(billingResult: BillingResult, purchases: MutableList<Purchase>?) {
        if (purchases == null) {
            return
        }
        processPurchases(billingResult, purchases)
    }

    override fun onAcknowledgePurchaseResponse(billingResult: BillingResult) {
        billingClient.queryPurchasesAsync(
            QueryPurchasesParams.newBuilder()
                .setProductType(BillingClient.ProductType.INAPP)
                .build(), this
        )
    }

}
