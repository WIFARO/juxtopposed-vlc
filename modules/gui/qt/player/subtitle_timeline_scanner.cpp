// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "subtitle_timeline_scanner.hpp"
#include "../../../codec/libass_plaintext.h"
#include <vlc_block.h>
#include <vlc_codec.h>
#include <vlc_configuration.h>
#include <vlc_demux.h>
#include <vlc_es_out.h>
#include <vlc_objects.h>
#include <vlc_stream.h>
#include <vlc_subpicture.h>
#include <vlc_text_style.h>
#include <QString>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace {
struct Output;
struct SubtitleDecoder
{
    decoder_t decoder;
    es_format_t input;
    Output *output;
};

struct Track
{
    es_format_t format;
    bool selected;
    SubtitleDecoder *decoder = nullptr;
    bool drained = false;
    Track(const es_format_t& source, bool selected) : selected(selected) { es_format_Copy(&format, &source); }
    ~Track() {
        if (decoder) {
            decoder_Clean(&decoder->decoder);
            es_format_Clean(&decoder->input);
            vlc_object_delete(&decoder->decoder);
        }
        es_format_Clean(&format);
    }
};

struct Output : es_out_t
{
    vlc_object_t *parent;
    const SubtitleTimelineSource& source;
    demux_t *demux = nullptr;
    int subtitleCount = 0;
    bool selectedFound = false;
    bool failed = false;
    std::atomic_bool limitReached{false};
    std::vector<std::unique_ptr<Track>> tracks;
    QVector<SubtitleTimelineCue> cues;
    qsizetype pending = -1;
    qsizetype textCharactersRemaining = 4 * 1024 * 1024;
    vlc_tick_t assTextPTS = VLC_TICK_INVALID;
    QString assText;
    std::mutex mutex;
    QVector<subpicture_t *> pictures;
    video_format_t videoFormat;
    video_format_t displayFormat;

    Output(vlc_object_t *parent, const SubtitleTimelineSource& source);
    ~Output() {
        tracks.clear();
        for (auto *picture : pictures) subpicture_Delete(picture);
        video_format_Clean(&videoFormat);
        video_format_Clean(&displayFormat);
    }

    void enqueue(subpicture_t *picture) {
        std::lock_guard<std::mutex> lock(mutex);
        if (pictures.size() < source.maximumCues) pictures.append(picture);
        else { limitReached = true; subpicture_Delete(picture); }
    }

    void processPictures() {
        QVector<subpicture_t *> pendingPictures;
        {
            std::lock_guard<std::mutex> lock(mutex);
            pendingPictures.swap(pictures);
        }
        // Resolve updater regions after pf_decode returns, outside codec locks.
        for (auto *picture : pendingPictures) {
            if (vlc_killed()) subpicture_Delete(picture);
            else collect(picture);
        }
    }

    void rememberAssText(const block_t *packet) {
        assText.clear();
        assTextPTS = VLC_TICK_INVALID;
        if (packet->i_flags & BLOCK_FLAG_CORRUPTED
            || packet->i_pts < VLC_TICK_0 || packet->i_buffer > 64 * 1024) return;
        // The SSA decoder receives Matroska-style chunks: ReadOrder, Layer,
        // Style, Name, MarginL, MarginR, MarginV, Effect, then dialogue text.
        const QByteArray chunk(reinterpret_cast<const char *>(packet->p_buffer), packet->i_buffer);
        qsizetype offset = 0;
        for (int i = 0; i < 8; ++i) {
            offset = chunk.indexOf(',', offset);
            if (offset < 0) return;
            ++offset;
        }
        char *plain = AssPlainText(chunk.constData() + offset);
        if (!plain) return;
        assText = QString::fromUtf8(plain).left(std::min<qsizetype>(2048, textCharactersRemaining)).simplified();
        free(plain);
        if (!assText.isEmpty() && assText.back().isHighSurrogate()) assText.chop(1);
        assTextPTS = packet->i_pts - VLC_TICK_0;
        // A decoder can add another event at the same timestamp without
        // queuing a second picture. Keep its dialogue in that scene's label.
        if (!cues.isEmpty() && cues.back().start == assTextPTS && !assText.isEmpty()) {
            auto &caption = cues.back().text;
            if (caption == assText) { assText.clear(); return; }
            const qsizetype prior = caption.size();
            const auto limit = std::min<qsizetype>(2048, prior + textCharactersRemaining);
            caption = (caption.isEmpty() ? assText : caption + QStringLiteral(" · ") + assText).left(limit);
            if (!caption.isEmpty() && caption.back().isHighSurrogate()) caption.chop(1);
            textCharactersRemaining -= caption.size() - prior;
            assText.clear();
        }
    }

    void collect(subpicture_t *picture) {
        std::lock_guard<std::mutex> lock(mutex);
        if (picture->i_start >= VLC_TICK_0) {
            const vlc_tick_t start = picture->i_start - VLC_TICK_0;
            // Ephemeral scenes end when replaced, including empty clear scenes.
            if (pending >= 0) {
                auto& prior = cues[pending];
                prior.stop = prior.stop == VLC_TICK_INVALID ? start : std::min(prior.stop, start);
                pending = -1;
            }
            // Text decoders can queue an updater for an empty clear packet.
            // Resolve the regions before deciding whether text is present.
            const vlc_tick_t sample = picture->i_stop > picture->i_start
                ? picture->i_start + (picture->i_stop - picture->i_start) / 2 : picture->i_start;
            subpicture_Update(picture, &videoFormat, &displayFormat, 640, 360, sample);
            bool content = false;
            QString caption;
            const qsizetype textLimit = std::min<qsizetype>(2048, textCharactersRemaining);
            subpicture_region_t *region;
            vlc_spu_regions_foreach(region, &picture->regions) {
                if (region->p_picture) content = true;
                if (!caption.isEmpty() && caption.size() < textLimit) caption += QLatin1Char(' ');
                for (const text_segment_t *text = region->p_text; text; text = text->p_next) {
                    if (!text->psz_text) continue;
                    const QString part = QString::fromUtf8(text->psz_text);
                    if (!part.trimmed().isEmpty()) content = true;
                    if (caption.size() < textLimit) caption += part.left(textLimit - caption.size());
                }
            }
            caption = caption.simplified();
            if (caption.isEmpty() && assTextPTS == start) caption = assText.left(textLimit);
            // Never retain an incomplete UTF-16 character at the text bound.
            if (!caption.isEmpty() && caption.back().isHighSurrogate()) caption.chop(1);
            if (content) {
                const vlc_tick_t stop = picture->i_stop > picture->i_start ? picture->i_stop - VLC_TICK_0 : VLC_TICK_INVALID;
                if (cues.size() < source.maximumCues) {
                    textCharactersRemaining -= caption.size();
                    cues.append({start, stop, std::move(caption)});
                    if (picture->b_ephemer || stop == VLC_TICK_INVALID) pending = cues.size() - 1;
                } else limitReached = true;
            }
        }
        subpicture_Delete(picture);
    }

    bool createDecoder(Track& track) {
        auto *owner = static_cast<SubtitleDecoder *>(vlc_object_create(parent, sizeof(SubtitleDecoder)));
        if (!owner) return false;
        owner->output = this;
        decoder_Init(&owner->decoder, &owner->input, &track.format);
        static const decoder_owner_callbacks callbacks = [] {
            decoder_owner_callbacks callbacks{};
            callbacks.spu.buffer_new = [](decoder_t *, const subpicture_updater_t *updater) { return subpicture_New(updater); };
            callbacks.spu.queue = [](decoder_t *decoder, subpicture_t *picture) {
                reinterpret_cast<SubtitleDecoder *>(decoder)->output->enqueue(picture);
            };
            callbacks.get_attachments = [](decoder_t *decoder, input_attachment_t ***attachments, int *count) {
                auto *output = reinterpret_cast<SubtitleDecoder *>(decoder)->output;
                return output->demux ? demux_Control(output->demux, DEMUX_GET_ATTACHMENTS, attachments, count) : VLC_EGENERIC;
            };
            return callbacks;
        }();
        owner->decoder.cbs = &callbacks;
        if (decoder_LoadModule(&owner->decoder, false, true) != VLC_SUCCESS) {
            decoder_Clean(&owner->decoder);
            es_format_Clean(&owner->input);
            vlc_object_delete(&owner->decoder);
            return false;
        }
        track.decoder = owner;
        return true;
    }

    void drain(Track& track) {
        if (track.decoder && !track.drained) {
            track.decoder->decoder.pf_decode(&track.decoder->decoder, nullptr);
            processPictures();
            track.drained = true;
        }
    }
};

Output::Output(vlc_object_t *parent, const SubtitleTimelineSource& source)
    : parent(parent), source(source)
{
    video_format_Init(&videoFormat, VLC_CODEC_RGBA);
    video_format_Setup(&videoFormat, VLC_CODEC_RGBA, source.videoWidth, source.videoHeight, source.videoWidth, source.videoHeight, 1, 1);
    video_format_Init(&displayFormat, VLC_CODEC_RGBA);
    video_format_Setup(&displayFormat, VLC_CODEC_RGBA, 640, 360, 640, 360, 1, 1);
    static const es_out_callbacks callbacks = [] {
        es_out_callbacks callbacks{};
        callbacks.add = [](es_out_t *base, input_source_t *, const es_format_t *format) -> es_out_id_t * {
            auto *output = static_cast<Output *>(base);
            const bool selected = format->i_cat == SPU_ES && output->subtitleCount++ == output->source.subtitleIndex;
            auto track = std::make_unique<Track>(*format, selected);
            auto *id = reinterpret_cast<es_out_id_t *>(track.get());
            output->selectedFound |= selected;
            output->tracks.push_back(std::move(track));
            return id;
        };
        callbacks.send = [](es_out_t *base, es_out_id_t *id, block_t *packet) {
            auto *output = static_cast<Output *>(base);
            auto *track = reinterpret_cast<Track *>(id);
            if (!track->selected || output->failed || output->limitReached || vlc_killed()) {
                block_Release(packet);
                return VLC_SUCCESS;
            }
            if (!track->decoder && !output->createDecoder(*track)) {
                output->failed = true;
                block_Release(packet);
                return VLC_EGENERIC;
            }
            if (track->format.i_codec == VLC_CODEC_SSA) output->rememberAssText(packet);
            int status = track->decoder->decoder.pf_decode(&track->decoder->decoder, packet);
            output->processPictures();
            if (status == VLCDEC_RELOAD) {
                // Reload retains ownership of the packet. Do not leak it.
                block_Release(packet);
                output->failed = true;
            } else if (status != VLCDEC_SUCCESS) output->failed = true;
            return output->failed ? VLC_EGENERIC : VLC_SUCCESS;
        };
        callbacks.del = [](es_out_t *base, es_out_id_t *id) {
            auto *output = static_cast<Output *>(base);
            auto *track = reinterpret_cast<Track *>(id);
            auto found = std::find_if(output->tracks.begin(), output->tracks.end(), [track](const auto& item) { return item.get() == track; });
            if (found != output->tracks.end()) { output->drain(**found); output->tracks.erase(found); }
        };
        callbacks.control = [](es_out_t *, input_source_t *, int query, va_list args) {
            switch (query) {
            case ES_OUT_GET_ES_STATE: {
                auto *track = reinterpret_cast<Track *>(va_arg(args, es_out_id_t *));
                *va_arg(args, bool *) = track && track->selected;
                return VLC_SUCCESS;
            }
            case ES_OUT_IS_EMPTY:
                *va_arg(args, bool *) = true;
                return VLC_SUCCESS;
            case ES_OUT_SET_ES_FMT: {
                auto *track = reinterpret_cast<Track *>(va_arg(args, es_out_id_t *));
                const auto *format = va_arg(args, const es_format_t *);
                if (!track || track->decoder) return VLC_EGENERIC;
                es_format_Clean(&track->format);
                return es_format_Copy(&track->format, format);
            }
            case ES_OUT_SET_ES:
            case ES_OUT_UNSET_ES:
            case ES_OUT_SET_ES_DEFAULT:
            case ES_OUT_SET_ES_STATE:
            case ES_OUT_SET_ES_CAT_POLICY:
            case ES_OUT_SET_GROUP:
            case ES_OUT_SET_PCR:
            case ES_OUT_SET_GROUP_PCR:
            case ES_OUT_RESET_PCR:
            case ES_OUT_SET_NEXT_DISPLAY_TIME:
            case ES_OUT_SET_GROUP_META:
            case ES_OUT_SET_META:
                return VLC_SUCCESS;
            default: return VLC_EGENERIC;
            }
        };
        callbacks.destroy = [](es_out_t *) {};
        return callbacks;
    }();
    cbs = &callbacks;
}
}

SubtitleTimelineScan scanSubtitleTimeline(vlc_object_t *parent,
                                         const SubtitleTimelineSource& source,
                                         vlc_interrupt_t *interrupt)
{
    SubtitleTimelineScan result;
    auto *previous = vlc_interrupt_set(interrupt);
    if (vlc_killed()) { vlc_interrupt_set(previous); result.status = SubtitleTimelineScan::Cancelled; return result; }
    if (source.maximumCues <= 0 || (source.deadline != VLC_TICK_INVALID && vlc_tick_now() >= source.deadline)) {
        vlc_interrupt_set(previous);
        result.status = SubtitleTimelineScan::LimitReached;
        return result;
    }
    auto *context = static_cast<vlc_object_t *>(vlc_object_create(parent, sizeof(vlc_object_t)));
    if (!context) { vlc_interrupt_set(previous); return result; }
    context->no_interact = true;
    for (auto it = source.options.cbegin(); it != source.options.cend(); ++it) {
        const auto name = it.key().toUtf8();
        const int type = config_GetType(name.constData());
        if (!type) continue;
        var_Create(context, name.constData(), type);
        switch (type) {
        case VLC_VAR_BOOL: var_SetBool(context, name.constData(), it.value().toBool()); break;
        case VLC_VAR_INTEGER: var_SetInteger(context, name.constData(), it.value().toLongLong()); break;
        case VLC_VAR_FLOAT: var_SetFloat(context, name.constData(), it.value().toFloat()); break;
        case VLC_VAR_STRING: var_SetString(context, name.constData(), it.value().toString().toUtf8().constData()); break;
        }
    }
    // Demux headers can inherit source options, but this reader has no outputs.
    var_Create(context, "interact", VLC_VAR_BOOL);
    var_SetBool(context, "interact", false);
    var_Create(context, "sub-original-fps", VLC_VAR_FLOAT);
    var_SetFloat(context, "sub-original-fps", source.originalFPS);
    {
        Output output(context, source);
        stream_t *stream = vlc_stream_NewURL(context, source.uri.constData());
        if (stream) {
            output.demux = demux_New(context, source.demux.constData(), source.uri.constData(), stream, &output);
            if (output.demux) {
                if (source.subtitleRate != 1) {
                    float rate = source.subtitleRate;
                    demux_Control(output.demux, DEMUX_SET_RATE, &rate);
                }
                int status = VLC_DEMUXER_SUCCESS;
                while (status > 0 && !vlc_killed() && !output.failed && !output.limitReached) {
                    if (source.deadline != VLC_TICK_INVALID && vlc_tick_now() >= source.deadline) { output.limitReached = true; break; }
                    status = demux_Demux(output.demux);
                }
                for (auto& track : output.tracks) output.drain(*track);
                vlc_tick_t duration = source.duration;
                if (duration <= 0) demux_Control(output.demux, DEMUX_GET_LENGTH, &duration);
                demux_Delete(output.demux);
                output.demux = nullptr;
                output.tracks.clear();
                if (output.pending >= 0 && output.cues[output.pending].stop == VLC_TICK_INVALID)
                    output.cues[output.pending].stop = duration;
                result.status = vlc_killed() ? SubtitleTimelineScan::Cancelled
                    : output.limitReached ? SubtitleTimelineScan::LimitReached
                    : output.failed || status < 0 ? SubtitleTimelineScan::Failed
                    : !output.selectedFound ? SubtitleTimelineScan::TrackMissing : SubtitleTimelineScan::Complete;
                result.cues = std::move(output.cues);
                result.cues.erase(std::remove_if(result.cues.begin(), result.cues.end(), [](const auto& cue) { return cue.stop <= cue.start; }), result.cues.end());
                std::sort(result.cues.begin(), result.cues.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
            } else vlc_stream_Delete(stream);
        }
        if (vlc_killed()) result.status = SubtitleTimelineScan::Cancelled;
    }
    vlc_object_delete(context);
    vlc_interrupt_set(previous);
    return result;
}
