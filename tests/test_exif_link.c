#include <libexif/exif-data.h>

#include <stddef.h>

int main(void)
{
    ExifData *data = exif_data_new();
    if (data == NULL) {
        return 1;
    }

    exif_data_set_byte_order(data, EXIF_BYTE_ORDER_INTEL);
    int result =
        exif_data_get_byte_order(data) == EXIF_BYTE_ORDER_INTEL ? 0 : 1;
    exif_data_unref(data);
    return result;
}
