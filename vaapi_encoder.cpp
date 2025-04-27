#include "vaapi_encoder.h"

#include <assert.h>
#include <cstring>
#include <vector>
#include <stdint.h>

#include <iostream>
#include <filesystem>

extern "C" {
#include <va/va.h>
#include <libavutil/opt.h>
}

enum {
    FOURCC_AVC = 1635148593,
    FOURCC_HEVC = 1752589105,
    FOURCC_AV1 = 1635135537,
};

static const uint8_t uuid_h264[] = { 0x01, 0x6f, 0x34, 0x71, 0x31, 0x17, 0x42, 0x05, 0xbf, 0x55, 0x37, 0x1c, 0xb0, 0xac, 0x66, 0x20 };
static const uint8_t uuid_hevc_8[] = { 0x01, 0x6f, 0x34, 0x71, 0x31, 0x17, 0x42, 0x05, 0xbf, 0x55, 0x37, 0x1c, 0xb0, 0xac, 0x66, 0x21 };
static const uint8_t uuid_hevc_10[] = { 0x01, 0x6f, 0x34, 0x71, 0x31, 0x17, 0x42, 0x05, 0xbf, 0x55, 0x37, 0x1c, 0xb0, 0xac, 0x66, 0x22 };
static const uint8_t uuid_av1_8[] = { 0x01, 0x6f, 0x34, 0x71, 0x31, 0x17, 0x42, 0x05, 0xbf, 0x55, 0x37, 0x1c, 0xb0, 0xac, 0x66, 0x23 };
static const uint8_t uuid_av1_10[] = { 0x01, 0x6f, 0x34, 0x71, 0x31, 0x17, 0x42, 0x05, 0xbf, 0x55, 0x37, 0x1c, 0xb0, 0xac, 0x66, 0x24 };

// H264 MP4
static bool ConvertAnnexBToAVCC(const uint8_t* annexb_data, size_t annexb_size,
                                std::vector<uint8_t>& out_avcc)
{
    if (!annexb_data || annexb_size < 6) return false;

    size_t offset = 0;
    std::vector<std::vector<uint8_t>> sps_list;
    std::vector<std::vector<uint8_t>> pps_list;

    while (offset + 4 < annexb_size) {
        if (annexb_data[offset] != 0 || annexb_data[offset+1] != 0 ||
            annexb_data[offset+2] != 0 || annexb_data[offset+3] != 1) {
            offset++;
        continue;
            }

            size_t nal_start = offset + 4;
            size_t nal_end = annexb_size;

            for (size_t i = nal_start; i + 4 < annexb_size; ++i) {
                if (annexb_data[i] == 0 && annexb_data[i+1] == 0 &&
                    annexb_data[i+2] == 0 && annexb_data[i+3] == 1) {
                    nal_end = i;
                break;
                    }
            }

            size_t nal_size = nal_end - nal_start;
            if (nal_size < 1) {
                offset = nal_end;
                continue;
            }

            uint8_t nal_type = annexb_data[nal_start] & 0x1F;
            std::vector<uint8_t> nal(annexb_data + nal_start, annexb_data + nal_end);

            if (nal_type == 7) sps_list.push_back(nal);
            else if (nal_type == 8) pps_list.push_back(nal);

            offset = nal_end;
    }

    if (sps_list.empty() || pps_list.empty()) return false;

    const std::vector<uint8_t>& sps = sps_list[0];

    out_avcc.clear();
    out_avcc.push_back(0x01);                // configurationVersion
    out_avcc.push_back(sps[1]);              // AVCProfileIndication
    out_avcc.push_back(sps[2]);              // profile_compatibility
    out_avcc.push_back(sps[3]);              // AVCLevelIndication
    out_avcc.push_back(0xFF);                // lengthSizeMinusOne = 4 bytes

    out_avcc.push_back(0xE0 | sps_list.size());
    for (const auto& s : sps_list) {
        out_avcc.push_back((s.size() >> 8) & 0xFF);
        out_avcc.push_back(s.size() & 0xFF);
        out_avcc.insert(out_avcc.end(), s.begin(), s.end());
    }

    out_avcc.push_back(pps_list.size());
    for (const auto& p : pps_list) {
        out_avcc.push_back((p.size() >> 8) & 0xFF);
        out_avcc.push_back(p.size() & 0xFF);
        out_avcc.insert(out_avcc.end(), p.begin(), p.end());
    }

    return true;
}

// HEVC MP4
static bool ConvertAnnexBToHVCC(const uint8_t* annexb_data, size_t annexb_size, std::vector<uint8_t>& out_hvcc)
{
    if (!annexb_data || annexb_size < 6) return false;

    size_t offset = 0;
    std::vector<std::vector<uint8_t>> vps_list;
    std::vector<std::vector<uint8_t>> sps_list;
    std::vector<std::vector<uint8_t>> pps_list;

    while (offset + 4 < annexb_size) {
        if (annexb_data[offset] == 0 && annexb_data[offset+1] == 0 &&
            annexb_data[offset+2] == 0 && annexb_data[offset+3] == 1) {
            offset += 4;
            } else if (annexb_data[offset+1] == 0 && annexb_data[offset+2] == 0 &&
                annexb_data[offset+3] == 1) {
                offset += 3;
                } else {
                    offset++;
                    continue;
                }

                size_t nal_start = offset;
            size_t nal_end = annexb_size;
            for (size_t i = offset; i + 4 < annexb_size; ++i) {
                if (annexb_data[i] == 0 && annexb_data[i+1] == 0 &&
                    annexb_data[i+2] == 0 && annexb_data[i+3] == 1) {
                    nal_end = i;
                break;
                    }
            }

            if (nal_end <= nal_start)
                continue;

        uint8_t nal_unit_type = (annexb_data[nal_start] >> 1) & 0x3F;
        std::vector<uint8_t> nal(annexb_data + nal_start, annexb_data + nal_end);

        switch (nal_unit_type) {
            case 32: vps_list.push_back(nal); break;
            case 33: sps_list.push_back(nal); break;
            case 34: pps_list.push_back(nal); break;
            default: break;
        }

        offset = nal_end;
    }

    if (sps_list.empty() || pps_list.empty() || vps_list.empty())
        return false;

    const std::vector<uint8_t>& sps = sps_list[0];

    uint8_t general_profile_space = (sps[1] >> 6) & 0x03;
    uint8_t general_tier_flag = (sps[1] >> 5) & 0x01;
    uint8_t general_profile_idc = sps[1] & 0x1F;
    uint32_t general_profile_compatibility = (sps[2] << 24) | (sps[3] << 16) | (sps[4] << 8) | sps[5];
    uint64_t general_constraint_indicator_flags = ((uint64_t)sps[6] << 40) | ((uint64_t)sps[7] << 32) |
    (sps[8] << 24) | (sps[9] << 16) | (sps[10] << 8) | sps[11];
    uint8_t general_level_idc = sps[12];

    out_hvcc.clear();
    out_hvcc.push_back(1); // configurationVersion
    out_hvcc.push_back((general_profile_space << 6) | (general_tier_flag << 5) | general_profile_idc);
    out_hvcc.push_back((general_profile_compatibility >> 24) & 0xFF);
    out_hvcc.push_back((general_profile_compatibility >> 16) & 0xFF);
    out_hvcc.push_back((general_profile_compatibility >> 8) & 0xFF);
    out_hvcc.push_back(general_profile_compatibility & 0xFF);

    for (int i = 5; i >= 0; --i)
        out_hvcc.push_back((general_constraint_indicator_flags >> (i * 8)) & 0xFF);

    out_hvcc.push_back(general_level_idc);
    out_hvcc.insert(out_hvcc.end(), {
        0xF0, // reserved (4 bits '1111') + min_spatial_segmentation_idc (12 bits)
    0x00, // min_spatial_segmentation_idc continued
    0xFC, // reserved (6 bits '111111') + parallelismType (2 bits)
    0xFD, // reserved (6 bits '111111') + chromaFormat (2 bits)
    0xF8, // reserved (5 bits '11111') + bitDepthLumaMinus8 (3 bits)
    0xF8, // reserved (5 bits '11111') + bitDepthChromaMinus8 (3 bits)
    0x00, 0x00, // avgFrameRate
    0x00, // constantFrameRate(2), numTemporalLayers(3), temporalIdNested(1), lengthSizeMinusOne(2)
    0x03  // numOfArrays
    });

    auto append_array = [&](uint8_t type, const std::vector<std::vector<uint8_t>>& list) {
        out_hvcc.push_back(0x80 | type); // array_completeness (1) + reserved + nal_unit_type
        out_hvcc.push_back(static_cast<uint8_t>(list.size()));
        for (const auto& nal : list) {
            out_hvcc.push_back((nal.size() >> 8) & 0xFF);
            out_hvcc.push_back(nal.size() & 0xFF);
            out_hvcc.insert(out_hvcc.end(), nal.begin(), nal.end());
        }
    };

    append_array(32, vps_list);
    append_array(33, sps_list);
    append_array(34, pps_list);

    return true;
}



class UISettingsController
{
public:
    UISettingsController()
    {
        InitDefaults();
    }

    explicit UISettingsController(const HostCodecConfigCommon& p_CommonProps)
        : m_CommonProps(p_CommonProps)
    {
        InitDefaults();
    }

    ~UISettingsController()
    {
    }

    void Load(IPropertyProvider* p_pValues)
    {
        uint8_t val8 = 0;
        p_pValues->GetUINT8("vaapi_reset", val8);
        if (val8 != 0) {
            *this = UISettingsController();
            return;
        }

        p_pValues->GetINT32("vaapi_preset", m_Preset);
        p_pValues->GetINT32("vaapi_preencode", m_PreEncode);
        p_pValues->GetINT32("vaapi_vbaq", m_VBAQ);
        p_pValues->GetINT32("vaapi_rc", m_RateControl);
        p_pValues->GetINT32("vaapi_qp", m_QP);
        p_pValues->GetINT32("vaapi_bitrate", m_BitRate);
        p_pValues->GetINT32("vaapi_device", m_Device);
    }

    StatusCode Render(HostListRef* p_pSettingsList)
    {
        {
            HostUIConfigEntryRef item("vaapi_device");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            for (int32_t i = 128; i < 140; i++) {
                std::string path = "/dev/dri/renderD" + std::to_string(i);
                if (std::filesystem::exists(path)) {
                    textsVec.push_back(path + "       ");
                    valuesVec.push_back(i);
                }
            }

            item.MakeComboBox("Device", textsVec, valuesVec, m_Device);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_separator");
            item.MakeSeparator();
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) {
                g_Log(logLevelError, "VAAPI :: Failed to add a separator entry");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item("vaapi_preset");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            textsVec.push_back("Speed");
            valuesVec.push_back(0);

            textsVec.push_back("Balanced");
            valuesVec.push_back(1);

            textsVec.push_back("Quality");
            valuesVec.push_back(2);

            item.MakeComboBox("Preset", textsVec, valuesVec, m_Preset);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_rc");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            textsVec.push_back("Constant Quality");
            valuesVec.push_back(0);

            textsVec.push_back("Variable Bitrate");
            valuesVec.push_back(1);

            item.MakeRadioBox("Rate Control", textsVec, valuesVec, GetRateControl());
            item.SetTriggersUpdate(true);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_qp");
            const char* pLabel = NULL;
            if (m_QP < 17) {
                pLabel = "(high)";
            } else if (m_QP < 34) {
                pLabel = "(medium)";
            } else {
                pLabel = "(low)";
            }
            item.MakeSlider("QP", pLabel, m_QP, 1, 51, 25);
            item.SetTriggersUpdate(true);
            item.SetHidden(m_RateControl != 0);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_bitrate");
            item.MakeSlider("Bit Rate", "Kbps", m_BitRate, 100, 100000, 8000, 1);
            item.SetHidden(m_RateControl != 1);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_separator");
            item.MakeSeparator();
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) {
                g_Log(logLevelError, "VAAPI :: Failed to add a separator entry");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item("vaapi_preencode");

            item.MakeCheckBox({}, "Enable PreEncode", m_PreEncode);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_vbaq");

            item.MakeCheckBox({}, "Enable VBAQ", m_VBAQ);
            item.SetHidden(m_RateControl != 1);

            p_pSettingsList->Append(&item);
        }

        {
            HostUIConfigEntryRef item("vaapi_reset");
            item.MakeButton("Reset");
            item.SetTriggersUpdate(true);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) {
                g_Log(logLevelError, "VAAPI :: Failed to populate the button entry");
                return errFail;
            }
        }

        return errNone;
    }

private:
    void InitDefaults()
    {
        m_Device = 128;
        m_Preset = 2;
        m_PreEncode = 1;
        m_VBAQ = 0;
        m_RateControl = 0;
        m_QP = 22;
        m_BitRate = 10000;
    }

public:
    int32_t GetDevice() const
    {
        return m_Device;
    }

    int32_t GetPreset() const
    {
        return m_Preset;
    }

    int32_t GetPreEncode() const
    {
        return m_PreEncode;
    }

    int32_t GetVBAQ() const
    {
        return m_VBAQ;
    }

    int32_t GetRateControl() const
    {
        return m_RateControl;
    }

    int32_t GetQP() const
    {
        return std::max<int>(0, m_QP);
    }

    int32_t GetBitRate() const
    {
        return m_BitRate;
    }

private:
    HostCodecConfigCommon m_CommonProps;
    int32_t m_Device;
    int32_t m_Preset;
    int32_t m_PreEncode;
    int32_t m_VBAQ;
    int32_t m_RateControl;
    int32_t m_QP;
    int32_t m_BitRate;
};

VAAPIEncoder::VAAPIEncoder(const char *name, uint32_t depth)
    : m_name(name)
    , m_depth(depth)
{
}

VAAPIEncoder::~VAAPIEncoder()
{
    av_buffer_unref(&m_hwdev);
    av_buffer_unref(&m_hwframes);
}

bool VAAPIEncoder::IsNeedNextPass()
{
    return false;
}

bool VAAPIEncoder::IsAcceptingFrame(int64_t p_PTS)
{
    return false;
}

VAAPIEncoder *VAAPIEncoder::Create(uint8_t *uuid)
{
    if (!memcmp(uuid, uuid_h264, 16))
        return new VAAPIEncoder("h264_vaapi", 8);
    else if (!memcmp(uuid, uuid_hevc_8, 16))
        return new VAAPIEncoder("hevc_vaapi", 8);
    else if (!memcmp(uuid, uuid_hevc_10, 16))
        return new VAAPIEncoder("hevc_vaapi", 10);
    else if (!memcmp(uuid, uuid_av1_8, 16))
        return new VAAPIEncoder("av1_vaapi", 8);
    else if (!memcmp(uuid, uuid_av1_10, 16))
        return new VAAPIEncoder("av1_vaapi", 10);
    return nullptr;
}

StatusCode VAAPIEncoder::GetEncoderSettings(uint8_t *uuid, HostPropertyCollectionRef *p_pValues, HostListRef *p_pSettingsList)
{
    HostCodecConfigCommon commonProps;
    commonProps.Load(p_pValues);

    UISettingsController settings(commonProps);
    settings.Load(p_pValues);

    return settings.Render(p_pSettingsList);
}

StatusCode VAAPIEncoder::RegisterCodecs(HostListRef *p_pList)
{
    g_Log(logLevelInfo, "VAAPI :: RegisterCodecs");

    auto addCodec = [&p_pList](const uint8_t *uuid, uint32_t fourcc, const char *group, const char *name, uint32_t depth) {
        HostPropertyCollectionRef info;
        info.SetProperty(pIOPropUUID, propTypeUInt8, uuid, 16);
        info.SetProperty(pIOPropName, propTypeString, name, strlen(name));
        info.SetProperty(pIOPropGroup, propTypeString, group, strlen(group));
        info.SetProperty(pIOPropFourCC, propTypeUInt32, &fourcc, 1);
        info.SetProperty(pIOPropBitDepth, propTypeUInt32, &depth, 1);
        info.SetProperty(pIOPropBitsPerSample, propTypeUInt32, &depth, 1);

        uint32_t mediaType = mediaVideo;
        info.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaType, 1);

        uint32_t direction = dirEncode;
        info.SetProperty(pIOPropCodecDirection, propTypeUInt32, &direction, 1);

        uint32_t colorModel = clrNV12;
        info.SetProperty(pIOPropColorModel, propTypeUInt32, &colorModel, 1);

        uint8_t dataRange[] = {0, 1};
        info.SetProperty(pIOPropDataRange, propTypeUInt8, &dataRange, sizeof(dataRange));;

        uint32_t bFrames = 0;
        info.SetProperty(pIOPropTemporalReordering, propTypeUInt32, &bFrames, 1);

        uint8_t fieldOrder = fieldProgressive;
        info.SetProperty(pIOPropFieldOrder, propTypeUInt8, &fieldOrder, 1);

        // ??
        std::vector<std::string> containerVec;
        containerVec.push_back("mp4");
        containerVec.push_back("mov");
        std::string valStrings;
        for (size_t i = 0; i < containerVec.size(); ++i) {
            valStrings.append(containerVec[i]);
            if (i < (containerVec.size() - 1)) {
                valStrings.append(1, '\0');
            }
        }
        info.SetProperty(pIOPropContainerList, propTypeString, valStrings.c_str(), static_cast<int>(valStrings.size()));

        p_pList->Append(&info);
    };

    addCodec(uuid_h264, FOURCC_AVC, "VAAPI H.264", "YUV 420 8-bit", 8);
    addCodec(uuid_hevc_8, FOURCC_HEVC, "VAAPI H.265", "YUV 420 8-bit", 8);
    addCodec(uuid_hevc_10, FOURCC_HEVC, "VAAPI H.265", "YUV 420 10-bit", 10);
    addCodec(uuid_av1_8, FOURCC_AV1, "VAAPI AV1", "YUV 420 8-bit", 8);
    addCodec(uuid_av1_10, FOURCC_AV1, "VAAPI AV1", "YUV 420 10-bit", 10);

    return errNone;
}


StatusCode VAAPIEncoder::DoInit(HostPropertyCollectionRef *p_pProps)
{
    g_Log(logLevelInfo, "VAAPI :: DoInit");

    uint32_t colorModel = clrNV12;
    p_pProps->SetProperty(pIOPropColorModel, propTypeUInt32, &colorModel, 1);

    uint8_t hSampling = 2;
    uint8_t vSampling = 2;
    p_pProps->SetProperty(pIOPropHSubsampling, propTypeUInt8, &hSampling, 1);
    p_pProps->SetProperty(pIOPropVSubsampling, propTypeUInt8, &vSampling, 1);

    return errNone;
}

StatusCode VAAPIEncoder::DoOpen(HostBufferRef *p_pBuff)
{
    g_Log(logLevelInfo, "VAAPI :: DoOpen");

    m_CommonProps.Load(p_pBuff);

    UISettingsController settings(m_CommonProps);
    settings.Load(p_pBuff);
    std::string container;
    if (p_pBuff->GetString(pIOPropContainerList, container)) {
        g_Log(logLevelInfo, "✅ Selected container: %s\n", container.c_str());
        m_containerFormat = container;
    } else {
        g_Log(logLevelError, "❌ Failed to retrieve container from pIOPropContainerList\n");
    }

    int16_t primaries = 0;
    if (!p_pBuff->GetINT16(pIOPropColorPrimaries, primaries))
        return errNoParam;

    int16_t trc = 0;
    if (!p_pBuff->GetINT16(pIOTransferCharacteristics, trc))
        return errNoParam;

    int16_t matrix = 0;
    if (!p_pBuff->GetINT16(pIOColorMatrix, matrix))
        return errNoParam;

    std::string path = "/dev/dri/renderD" + std::to_string(settings.GetDevice());
    int err = av_hwdevice_ctx_create(&m_hwdev, AV_HWDEVICE_TYPE_VAAPI, path.c_str(), NULL, 0);
    if (err != 0) {
        g_Log(logLevelError, "VAAPI :: Failed to open device %d", err);
        return errFail;
    }

    const AVCodec *codec = avcodec_find_encoder_by_name(m_name);
    if (!codec) {
        g_Log(logLevelError, "VAAPI :: Failed to find encoder '%s'", m_name);
        return errFail;
    }

    m_codec = avcodec_alloc_context3(codec);
    if (!m_codec)
        return errFail;

    m_codec->width = m_CommonProps.GetWidth();
    m_codec->height = m_CommonProps.GetHeight();
    m_codec->time_base = av_make_q(m_CommonProps.GetFrameRateDen(), m_CommonProps.GetFrameRateNum());
    m_codec->framerate = av_make_q(m_CommonProps.GetFrameRateNum(), m_CommonProps.GetFrameRateDen());
    m_codec->sample_aspect_ratio = av_make_q(1, 1);
    m_codec->pix_fmt = AV_PIX_FMT_VAAPI;
    m_codec->flags = AV_CODEC_FLAG_GLOBAL_HEADER;
    m_codec->max_b_frames = 0;
    m_codec->gop_size = 300;
    m_codec->color_range = m_CommonProps.IsFullRange() ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    m_codec->color_primaries = static_cast<enum AVColorPrimaries>(primaries);
    m_codec->color_trc = static_cast<enum AVColorTransferCharacteristic>(trc);
    m_codec->colorspace = static_cast<enum AVColorSpace>(matrix);
    m_codec->compression_level = settings.GetPreset() << 1 | settings.GetPreEncode() << 3 | settings.GetVBAQ() << 4;

    if (settings.GetRateControl() == 0) {
        av_opt_set(m_codec->priv_data, "rc_mode", "CQP", 0);
        m_codec->global_quality = settings.GetQP();
    } else {
        av_opt_set(m_codec->priv_data, "rc_mode", "VBR", 0);
        m_codec->bit_rate = settings.GetBitRate() * 1000;
        m_codec->rc_max_rate = m_codec->bit_rate * 1.5;
        m_codec->rc_buffer_size = m_codec->rc_max_rate;
    }

    m_hwframes = av_hwframe_ctx_alloc(m_hwdev);
    if (!m_hwframes) {
        g_Log(logLevelError, "VAAPI :: Failed to create frames context");
        return errFail;
    }

    m_format = m_depth == 8 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_P010;

    AVHWFramesContext *framesCtx = reinterpret_cast<AVHWFramesContext*>(m_hwframes->data);
    framesCtx->format = AV_PIX_FMT_VAAPI;
    framesCtx->sw_format = m_format;
    framesCtx->width = m_codec->width;
    framesCtx->height = m_codec->height;

    err = av_hwframe_ctx_init(m_hwframes);
    if (err != 0) {
        g_Log(logLevelError, "VAAPI :: Failed to init frames %d", err);
        return errFail;
    }

    m_codec->hw_frames_ctx = m_hwframes;

    err = avcodec_open2(m_codec, codec, NULL);
    if (err != 0) {
        g_Log(logLevelError, "VAAPI :: Failed to open encoder %d", err);
        return errFail;
    }

    if (m_codec->extradata_size) {
        if (m_containerFormat == "mp4") {
            if (!strcmp(m_name, "h264_vaapi")) {
                std::vector<uint8_t> avcc;
                if (ConvertAnnexBToAVCC(m_codec->extradata, m_codec->extradata_size, avcc)) {
                    p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8, avcc.data(), static_cast<int>(avcc.size()));
                    m_configExtradata = std::move(avcc);
                    m_sentFirstPacket = false;
                } else {
                    g_Log(logLevelError, "VAAPI :: Failed to convert H.264 extradata to AVCC");
                }
            } else if (!strcmp(m_name, "hevc_vaapi")) {
                std::vector<uint8_t> hvcc;
                if (ConvertAnnexBToHVCC(m_codec->extradata, m_codec->extradata_size, hvcc)) {
                    p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8, hvcc.data(), static_cast<int>(hvcc.size()));
                    m_configExtradata = std::move(hvcc);
                    m_sentFirstPacket = false;
                } else {
                    g_Log(logLevelError, "VAAPI :: Failed to convert HEVC extradata to hvcC");
                }
            } else {
                // AV1 MP4
                p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8, m_codec->extradata, m_codec->extradata_size);
            }
        } else {
            // MOV
            p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8, m_codec->extradata, m_codec->extradata_size);
        }
    }

    uint8_t multiPass = 0;
    p_pBuff->SetProperty(pIOPropMultiPass, propTypeUInt8, &multiPass, 1);

    uint32_t bFrames = 0;
    p_pBuff->SetProperty(pIOPropTemporalReordering, propTypeUInt32, &bFrames, 1);

    return errNone;
}

StatusCode VAAPIEncoder::DoProcess(HostBufferRef *p_pBuff)
{
    if (!m_codec)
        return errFail;

    if (!p_pBuff || !p_pBuff->IsValid()) {
        g_Log(logLevelInfo, "VAAPI :: Flush");
        avcodec_send_frame(m_codec, nullptr);
        return ReceiveData();
    }

    uint32_t width;
    if (!p_pBuff->GetUINT32(pIOPropWidth, width))
        return errNoParam;

    uint32_t height;
    if (!p_pBuff->GetUINT32(pIOPropHeight, height))
        return errNoParam;

    int64_t pts;
    if (!p_pBuff->GetINT64(pIOPropPTS, pts))
        return errNoParam;

    char *buf = nullptr;
    size_t bufSize = 0;
    if (!p_pBuff->LockBuffer(&buf, &bufSize)) {
        g_Log(logLevelError, "VAAPI :: Failed to lock the buffer");
        return errFail;
    }

    AVFrame *swFrame = av_frame_alloc();
    if (!swFrame) {
        p_pBuff->UnlockBuffer();
        return errFail;
    }

    swFrame->width = width;
    swFrame->height = height;
    swFrame->format = m_format;

    uint32_t bpp = m_depth == 8 ? 1 : 2;

    swFrame->data[0] = reinterpret_cast<uint8_t*>(buf);
    swFrame->data[1] = reinterpret_cast<uint8_t*>(buf) + (width * height * bpp);
    swFrame->linesize[0] = width * bpp;
    swFrame->linesize[1] = width * bpp;

    AVFrame *hwFrame = av_frame_alloc();
    if (!hwFrame) {
        p_pBuff->UnlockBuffer();
        return errFail;
    }

    int err = av_hwframe_get_buffer(m_hwframes, hwFrame, 0);
    if (err != 0) {
        p_pBuff->UnlockBuffer();
        g_Log(logLevelError, "VAAPI :: Failed to get hw buffer %d", err);
        return errFail;
    }

    err = av_hwframe_transfer_data(hwFrame, swFrame, 0);
    p_pBuff->UnlockBuffer();
    if (err != 0) {
        g_Log(logLevelError, "VAAPI :: Failed to upload buffer %d", err);
        return errFail;
    }

    hwFrame->pts = pts;

    err = avcodec_send_frame(m_codec, hwFrame);
    if (err != 0) {
        g_Log(logLevelError, "VAAPI :: Failed to encode frame %d", err);
        return errFail;
    }

    av_frame_free(&hwFrame);
    av_frame_free(&swFrame);

    return ReceiveData();
}

void VAAPIEncoder::DoFlush()
{
    g_Log(logLevelInfo, "VAAPI :: DoFlush");
}

StatusCode VAAPIEncoder::ReceiveData()
{
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return errFail;

    bool haveOutput = false;
    StatusCode status = errNone;

    while (true) {
        int err = avcodec_receive_packet(m_codec, pkt);
        if (err) {
            if (err == AVERROR(EAGAIN)) {
                status = haveOutput ? errNone : errMoreData;
            } else if (err == AVERROR_EOF) {
                status = errNone;
            } else {
                g_Log(logLevelError, "VAAPI :: Failed to receive packet %d", err);
                status = errFail;
            }
            break;
        }

        HostBufferRef outBuf;
        if (!outBuf.IsValid() || !outBuf.Resize(pkt->size))
            return errAlloc;

        uint8_t isKeyFrame = pkt->flags & AV_PKT_FLAG_KEY;

        char *buf = nullptr;
        size_t bufSize = 0;
        if (!m_sentFirstPacket && isKeyFrame && !m_configExtradata.empty() && m_containerFormat == "mp4") {
            size_t totalSize = m_configExtradata.size() + pkt->size;
            if (!outBuf.Resize(totalSize)) return errAlloc;

            if (!outBuf.LockBuffer(&buf, &bufSize)) return errAlloc;

            memcpy(buf, m_configExtradata.data(), m_configExtradata.size());
            memcpy(buf + m_configExtradata.size(), pkt->data, pkt->size);

            m_sentFirstPacket = true;
            g_Log(logLevelInfo, "VAAPI :: Injected extradata into first keyframe");
        } else {
            if (!outBuf.LockBuffer(&buf, &bufSize)) return errAlloc;
            memcpy(buf, pkt->data, pkt->size);
        }

        outBuf.SetProperty(pIOPropPTS, propTypeInt64, &pkt->pts, 1);
        outBuf.SetProperty(pIOPropDTS, propTypeInt64, &pkt->dts, 1);
        outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKeyFrame, 1);

        av_packet_unref(pkt);

        status = m_pCallback->SendOutput(&outBuf);
        if (status != errNone)
            break;
        haveOutput = true;
    }

    av_packet_free(&pkt);

    return status;
}
