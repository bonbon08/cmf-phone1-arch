// CMF Phone 1 (cmf-phone1-arch): no hardware video decoding for Firefox. The MediaTek
// codecs are Android HALs; probing all ~170 /dev/video* nodes at every start took ~10 s.
pref("media.hardware-video-decoding.enabled", false);
pref("media.ffmpeg.vaapi.enabled", false);
pref("media.ffmpeg.v4l2.enabled", false);
