#include "plugin.h"
#include "vaapi_encoder.h"

StatusCode g_HandleGetInfo(HostPropertyCollectionRef* p_pProps)
{
    static const uint8_t uuid[] = { 0x01, 0x6f, 0x34, 0x71, 0x31, 0x17, 0x42, 0x05, 0xbf, 0x55, 0x37, 0x1c, 0xb0, 0xac, 0x66, 0x1a };
    StatusCode err = p_pProps->SetProperty(pIOPropUUID, propTypeUInt8, uuid, 16);
    if (err == errNone)
        err = p_pProps->SetProperty(pIOPropName, propTypeString, "VAAPI Plugin", strlen("VAAPI Plugin"));
    return err;
}

StatusCode g_HandleCreateObj(unsigned char *p_pUUID, ObjectRef *p_ppObj)
{
    VAAPIEncoder *enc = VAAPIEncoder::Create(p_pUUID);
    if (enc) {
        *p_ppObj = enc;
        return errNone;
    }
    return errUnsupported;
}

StatusCode g_HandlePluginStart()
{
    return errNone;
}

StatusCode g_HandlePluginTerminate()
{
    return errNone;
}

StatusCode g_ListCodecs(HostListRef *p_pList)
{
    StatusCode err = VAAPIEncoder::RegisterCodecs(p_pList);
    if (err != errNone)
        return err;
    return errNone;
}

StatusCode g_ListContainers(HostListRef *p_pList)
{
    return errNone;
}

StatusCode g_GetEncoderSettings(unsigned char *p_pUUID, HostPropertyCollectionRef *p_pValues, HostListRef *p_pSettingsList)
{
    return VAAPIEncoder::GetEncoderSettings(p_pUUID, p_pValues, p_pSettingsList);
}
