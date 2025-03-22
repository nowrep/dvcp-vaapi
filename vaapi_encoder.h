#pragma once

#include <memory>

#include "wrapper/plugin_api.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}

using namespace IOPlugin;

class UISettingsController;

class VAAPIEncoder : public IPluginCodecRef
{
public:
    ~VAAPIEncoder();

    bool IsNeedNextPass() override;
    bool IsAcceptingFrame(int64_t p_PTS) override;

    static VAAPIEncoder *Create(uint8_t *uuid);
    static StatusCode RegisterCodecs(HostListRef *p_pList);
    static StatusCode GetEncoderSettings(uint8_t *uuid, HostPropertyCollectionRef *p_pValues, HostListRef *p_pSettingsList);

private:
    explicit VAAPIEncoder(const char *name, uint32_t depth);
    StatusCode DoInit(HostPropertyCollectionRef *p_pProps) override;
    StatusCode DoOpen(HostBufferRef *p_pBuff) override;
    StatusCode DoProcess(HostBufferRef *p_pBuff) override;
    void DoFlush() override;
    StatusCode ReceiveData();

    const char *m_name;
    uint32_t m_depth;

    enum AVPixelFormat m_format = AV_PIX_FMT_NONE;
    AVBufferRef *m_hwdev = nullptr;
    AVCodecContext *m_codec = nullptr;
    AVBufferRef *m_hwframes = nullptr;

    int m_ColorModel;
    std::unique_ptr<UISettingsController> m_pSettings;
    HostCodecConfigCommon m_CommonProps;
};
