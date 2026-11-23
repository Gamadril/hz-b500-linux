$(call inherit-product-if-exists, target/allwinner/d1s-common/d1s-common.mk)

PRODUCT_AAPT_CONFIG := large xlarge hdpi xhdpi
PRODUCT_AAPT_PERF_CONFIG := xhdpi
PRODUCT_CHARACTERISTICS := musicbox

PRODUCT_BRAND := allwinner
PRODUCT_NAME := d1s_hz_b500_sd
PRODUCT_DEVICE := d1s-hz-b500_sd
PRODUCT_MODEL := HZ B500
