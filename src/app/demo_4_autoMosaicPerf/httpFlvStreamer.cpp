#include "httpFlvStreamer.h"
#include "LogMacros.h"

#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <fstream>

extern "C" {
#include <libavutil/time.h>
}

namespace {
    // FLV Tag 类型
    const uint8_t FLV_TAG_TYPE_VIDEO = 0x09;
    const uint8_t FLV_TAG_TYPE_AUDIO = 0x08;
    const uint8_t FLV_TAG_TYPE_SCRIPT = 0x12;

    // 辅助函数：写入大端序数值
    void writeUint32BE(std::vector<uint8_t>& buf, uint32_t val) {
        buf.push_back((val >> 24) & 0xFF);
        buf.push_back((val >> 16) & 0xFF);
        buf.push_back((val >> 8) & 0xFF);
        buf.push_back(val & 0xFF);
    }

    void writeUint24BE(std::vector<uint8_t>& buf, uint32_t val) {
        buf.push_back((val >> 16) & 0xFF);
        buf.push_back((val >> 8) & 0xFF);
        buf.push_back(val & 0xFF);
    }

    // 查找下一个起始码的位置
    size_t findNextStartCode(const uint8_t* data, size_t size, size_t start_pos) {
        for (size_t i = start_pos; i + 3 < size; i++) {
            if (data[i] == 0x00 && data[i+1] == 0x00) {
                if (data[i+2] == 0x01) {
                    return i;
                }
                if (i + 3 < size && data[i+2] == 0x00 && data[i+3] == 0x01) {
                    return i;
                }
            }
        }
        return size;
    }
    
    // 按 AUD (NAL type 9) 切分 Access Units
    // 返回 true 如果找到一个 AU，offset 会移动到下一个 AU 的开始
    static bool splitByAUD(
        const std::vector<uint8_t>& data,
        size_t& offset,
        std::vector<uint8_t>& au)
    {
        au.clear();
        size_t size = data.size();
        if (offset >= size) return false;
        
        auto is_start_code = [&](size_t p, size_t& len) -> bool {
            if (p + 2 >= size) return false;
            if (data[p] == 0x00 && data[p+1] == 0x00) {
                if (data[p+2] == 0x01) {
                    len = 3;
                    return true;
                }
                if (p + 3 < size && data[p+2] == 0x00 && data[p+3] == 0x01) {
                    len = 4;
                    return true;
                }
            }
            return false;
        };
        
        size_t au_start = offset;
        bool found_first_aud = false;
        size_t current_pos = offset;
        
        while (current_pos < size) {
            size_t sc_len = 0;
            if (!is_start_code(current_pos, sc_len)) {
                current_pos++;
                continue;
            }
            
            // 检查 NAL type
            size_t nal_header_pos = current_pos + sc_len;
            if (nal_header_pos >= size) break;
            
            uint8_t nal_type = data[nal_header_pos] & 0x1F;
            
            if (nal_type == 9) { // AUD
                if (found_first_aud) {
                    // 找到下一个 AUD，切分点
                    au.assign(data.begin() + au_start, data.begin() + current_pos);
                    offset = current_pos;
                    LOG_DEBUG("Split AU: size=" << au.size() << ", next_offset=" << offset);
                    return true;
                }
                found_first_aud = true;
                au_start = current_pos; // AU 从第一个 AUD 开始
            }
            
            current_pos += sc_len;
        }
        
        // 如果到达文件末尾，返回剩余数据
        if (found_first_aud && current_pos >= size) {
            au.assign(data.begin() + au_start, data.end());
            offset = size;
            LOG_DEBUG("Split AU (last): size=" << au.size());
            return true;
        }
        
        // 没有找到 AUD，返回所有数据
        if (!found_first_aud && offset < size) {
            au.assign(data.begin() + offset, data.end());
            offset = size;
            LOG_WARN("No AUD found, treating entire buffer as one AU, size=" << au.size());
            return true;
        }
        
        return false;
    }

    static std::vector<uint8_t> convertAnnexBToAVCC(const std::vector<uint8_t>& annexb) {
    std::vector<uint8_t> out;
    const uint8_t* p = annexb.data();
    const uint8_t* end = p + annexb.size();

    int nal_count = 0;
    size_t total_nal_bytes = 0;
    size_t total_skipped_bytes = 0;

    while (p < end) {
        // 1. 跳过当前的起始码
        size_t sc_len = 0;
        if (p + 3 < end && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01) sc_len = 4;
        else if (p + 2 < end && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) sc_len = 3;
        
        if (sc_len > 0) {
            p += sc_len;
            const uint8_t* nal_start = p;
            
            // 2. 寻找下一个起始码
            const uint8_t* next_p = p;
            while (next_p < end - 2) {
                if (next_p[0] == 0x00 && next_p[1] == 0x00 && (next_p[2] == 0x01 || (next_p + 3 < end && next_p[2] == 0x00 && next_p[3] == 0x01))) {
                    break;
                }
                next_p++;
            }
            if (next_p > end - 3) next_p = end;

            // NAL unit 的结束位置就是下一个 start code 的开始，不需要去除尾部 0x00
            // 去除尾部 0x00 会导致 NAL size 计算错误，引发 "Invalid NAL unit size" 错误
            const uint8_t* nal_end = next_p;

            uint32_t nal_size = static_cast<uint32_t>(nal_end - nal_start);
            if (nal_size > 0) {
                // 检查 NAL type 并过滤 SEI/SPS/PPS
                uint8_t nal_header = *nal_start;
                uint8_t nal_type = nal_header & 0x1F;
                
                // Log every NAL unit for debugging
                LOG_DEBUG("NAL at offset " << (nal_start - annexb.data()) 
                         << ": type=" << (int)nal_type 
                         << ", size=" << nal_size
                         << ", header=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)nal_header << std::dec);
                
                // Skip SEI (type 6) and AUD (type 9) - they may contain encoder metadata that causes issues
                if (nal_type == 6 || nal_type == 9) {
                    LOG_DEBUG("Skipping NAL type " << (int)nal_type 
                             << (nal_type == 6 ? " (SEI)" : " (AUD)")
                             << " size=" << nal_size);
                    total_skipped_bytes += nal_size;
                    p = next_p;
                    continue;
                }
                
                // Verify NAL size is reasonable (< 10MB)
                if (nal_size > 10 * 1024 * 1024) {
                    LOG_ERROR("NAL size too large: " << nal_size << " bytes, skipping");
                    p = next_p;
                    continue;
                }
                
                // 写入 4 字节 AVCC 长度
                out.push_back((nal_size >> 24) & 0xFF);
                out.push_back((nal_size >> 16) & 0xFF);
                out.push_back((nal_size >> 8) & 0xFF);
                out.push_back(nal_size & 0xFF);
                
                // Log first 8 bytes of NAL data
                if (nal_size >= 8) {
                    std::stringstream ss;
                    ss << "NAL data first 8 bytes: ";
                    for (int i = 0; i < 8 && i < nal_size; i++) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << (int)nal_start[i] << " ";
                    }
                    LOG_DEBUG(ss.str());
                }
                
                // 写入负载
                out.insert(out.end(), nal_start, nal_end);
                
                total_nal_bytes += nal_size;
                nal_count++;
            }
            p = next_p; // 将指针移到下一个起始码开头
        } else {
            p++;
        }
    }
    
    // Summary log
    LOG_DEBUG("AVCC conversion: input=" << annexb.size() 
             << ", output=" << out.size()
             << ", NAL_count=" << nal_count
             << ", total_NAL_bytes=" << total_nal_bytes
             << ", skipped_bytes=" << total_skipped_bytes
             << ", overhead=" << (nal_count * 4));
    
    // Verify: out.size() should equal (nal_count * 4) + total_nal_bytes
    size_t expected_size = (nal_count * 4) + total_nal_bytes;
    if (out.size() != expected_size) {
        LOG_ERROR("AVCC size mismatch! expected=" << expected_size << ", actual=" << out.size());
    }
    
    return out;
}

    // 检查是否为关键帧 (I帧)
    bool isKeyFrame(const std::vector<uint8_t>& h264_data) {
        if (h264_data.size() < 5) return false;
        
        size_t i = 0;
        while (i < h264_data.size() - 4) {
            if (h264_data[i] != 0x00) { i++; continue; }
            
            size_t nal_start = 0;
            size_t start_code_len = 0;
            
            if (h264_data[i+1] == 0x00 && h264_data[i+2] == 0x01) {
                nal_start = i + 3;
                start_code_len = 3;
            } else if (h264_data[i+1] == 0x00 && h264_data[i+2] == 0x00 && h264_data[i+3] == 0x01) {
                nal_start = i + 4;
                start_code_len = 4;
            } else {
                i++; continue;
            }
            
            if (nal_start < h264_data.size()) {
                uint8_t nal_type = h264_data[nal_start] & 0x1F;
                
                // NAL type 5 = IDR slice (keyframe)
                if (nal_type == 5) {
                    return true;
                }
                i = nal_start; // 继续向后找
            } else {
                break;
            }
        }
        return false;
    }
    
    // 提取SPS和PPS NAL units
    struct SPSPPSData {
        std::vector<uint8_t> sps;
        std::vector<uint8_t> pps;
    };
    
    SPSPPSData extractSPSPPS(const std::vector<uint8_t>& h264_data) {
        SPSPPSData result;
        size_t i = 0;
        
        LOG_DEBUG("Extracting SPS/PPS from data of size: " << h264_data.size());
        
        while (i < h264_data.size() - 4) {
            if (h264_data[i] != 0x00) { i++; continue; }

            size_t nal_start = 0;
            size_t start_code_len = 0;
            
            if (h264_data[i+1] == 0x00 && h264_data[i+2] == 0x01) {
                nal_start = i + 3;
                start_code_len = 3;
            } else if (h264_data[i+1] == 0x00 && h264_data[i+2] == 0x00 && h264_data[i+3] == 0x01) {
                nal_start = i + 4;
                start_code_len = 4;
            } else {
                i++; continue;
            }
            
            if (nal_start >= h264_data.size()) break;
            
            uint8_t nal_type = h264_data[nal_start] & 0x1F;
            
            LOG_DEBUG("Found NAL unit type: " << (int)nal_type << " at position " << i);
            
            // 查找下一个start code
            size_t next_start = h264_data.size();
            for (size_t k = nal_start; k + 2 < h264_data.size(); ++k) {
                if (h264_data[k] == 0x00 && h264_data[k+1] == 0x00) {
                    if (h264_data[k+2] == 0x01) { next_start = k; break; }
                    if (k + 3 < h264_data.size() && h264_data[k+2] == 0x00 && h264_data[k+3] == 0x01) { next_start = k; break; }
                }
            }
            
            if (nal_type == 7 && result.sps.empty()) { // SPS
                result.sps.assign(h264_data.begin() + nal_start, h264_data.begin() + next_start);
                LOG_DEBUG("Extracted SPS, size: " << result.sps.size());
            } else if (nal_type == 8 && result.pps.empty()) { // PPS
                result.pps.assign(h264_data.begin() + nal_start, h264_data.begin() + next_start);
                LOG_DEBUG("Extracted PPS, size: " << result.pps.size());
            }
            
            if (!result.sps.empty() && !result.pps.empty()) {
                LOG_DEBUG("SPS and PPS extracted successfully");
                break;
            }
            
            i = (next_start == h264_data.size()) ? nal_start + 1 : next_start;
        }
        return result;
    }
    
    // 创建AVC Decoder Configuration Record (FLV tag)
    std::vector<uint8_t> createAVCDecoderConfigRecord(const std::vector<uint8_t>& sps_annexb, const std::vector<uint8_t>& pps_annexb) {
        if (sps_annexb.empty() || pps_annexb.empty()) {
            LOG_ERROR("Cannot create AVC config record: SPS or PPS is empty");
            return {};
        }
        
        LOG_INFO("Creating AVC Decoder Config Record:");
        LOG_INFO("  SPS size (Annex-B): " << sps_annexb.size());
        LOG_INFO("  PPS size (Annex-B): " << pps_annexb.size());
        
        // Debug: print first few bytes of SPS and PPS
        if (sps_annexb.size() >= 4) {
            std::stringstream ss;
            ss << "SPS first 4 bytes: ";
            for (size_t i = 0; i < 4; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)sps_annexb[i] << " ";
            }
            LOG_DEBUG(ss.str());
        }
        
        // Remove Annex-B start code from SPS and PPS
        auto remove_start_code = [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
            if (data.size() < 4) {
                LOG_WARN("Data too small to contain start code");
                return data;
            }
            
            // Check for 4-byte start code (00 00 00 01)
            if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
                return std::vector<uint8_t>(data.begin() + 4, data.end());
            }
            // Check for 3-byte start code (00 00 01)
            else if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
                return std::vector<uint8_t>(data.begin() + 3, data.end());
            }
            // No start code found, check if it's already raw NAL
            else {
                LOG_DEBUG("No start code found, assuming raw NAL data");
                return data;
            }
        };
        
        std::vector<uint8_t> sps = remove_start_code(sps_annexb);
        std::vector<uint8_t> pps = remove_start_code(pps_annexb);
        
        LOG_INFO("SPS (raw NAL) size: " << sps.size() << ", PPS (raw NAL) size: " << pps.size());
        
        if (sps.size() < 4 || pps.size() < 1) {
            LOG_ERROR("Invalid SPS/PPS data after removing start code");
            return {};
        }
        
        std::vector<uint8_t> flv_tag;
        flv_tag.reserve(64 + sps.size() + pps.size());
        
        // FLV Tag Header
        flv_tag.push_back(FLV_TAG_TYPE_VIDEO);
        
        // Data size placeholder (will update later)
        size_t data_size_pos = flv_tag.size();
        writeUint24BE(flv_tag, 0);
        
        // Timestamp (3 bytes + 1 extended, all zeros for config)
        writeUint24BE(flv_tag, 0);  // Lower 24 bits
        flv_tag.push_back(0);        // Upper 8 bits (Extended timestamp)
        // Stream ID
        writeUint24BE(flv_tag, 0);
        
        // Video Tag Data
        flv_tag.push_back(0x17);  // Keyframe + AVC
        flv_tag.push_back(0x00);  // AVC sequence header
        writeUint24BE(flv_tag, 0); // Composition time
        
        // AVCDecoderConfigurationRecord
        flv_tag.push_back(0x01);  // configurationVersion
        flv_tag.push_back(sps[1]);  // AVCProfileIndication
        flv_tag.push_back(sps[2]);  // profile_compatibility
        flv_tag.push_back(sps[3]);  // AVCLevelIndication
        flv_tag.push_back(0xFF);  // lengthSizeMinusOne (reserved 6 bits + 2 bits size=3 -> 4 bytes)
        
        // SPS
        flv_tag.push_back(0xE1);  // numOfSequenceParameterSets (1)
        flv_tag.push_back((sps.size() >> 8) & 0xFF);
        flv_tag.push_back(sps.size() & 0xFF);
        flv_tag.insert(flv_tag.end(), sps.begin(), sps.end());
        
        // PPS
        flv_tag.push_back(0x01);  // numOfPictureParameterSets (1)
        flv_tag.push_back((pps.size() >> 8) & 0xFF);
        flv_tag.push_back(pps.size() & 0xFF);
        flv_tag.insert(flv_tag.end(), pps.begin(), pps.end());
        
        // Update data size
        uint32_t data_size = (uint32_t)(flv_tag.size() - 11);
        flv_tag[data_size_pos] = (data_size >> 16) & 0xFF;
        flv_tag[data_size_pos + 1] = (data_size >> 8) & 0xFF;
        flv_tag[data_size_pos + 2] = data_size & 0xFF;
        
        // Previous tag size (tag size without the 4-byte PrevTagSize field itself)
        uint32_t prev_tag_size = (uint32_t)flv_tag.size();
        writeUint32BE(flv_tag, prev_tag_size);
        
        LOG_INFO("AVC Decoder Config Record created, total size: " << flv_tag.size() << " bytes");
        return flv_tag;
    }
    
    // 将单个 Access Unit 封装为 FLV video tag
    std::vector<uint8_t> createFlvVideoTagFromAU(const std::vector<uint8_t>& au_data, uint32_t timestamp, bool is_keyframe, int client_id) {
        // 首先验证数据是否有效
        if (au_data.empty()) {
            LOG_ERROR("Client " << client_id << ": Empty AU data received");
            return {};
        }
        
        LOG_DEBUG("Client " << client_id << ": Creating FLV video tag from AU, input size: " << au_data.size() 
                  << ", timestamp: " << timestamp << ", keyframe: " << is_keyframe);
        
        // 验证是否为有效的H.264数据（包含起始码）
        bool has_start_code = false;
        for (size_t i = 0; i < std::min<size_t>(au_data.size(), 16); i++) {
            if (i + 3 < au_data.size()) {
                if (au_data[i] == 0x00 && au_data[i+1] == 0x00) {
                    if (au_data[i+2] == 0x01) {
                        has_start_code = true;
                        break;
                    }
                    if (i + 4 < au_data.size() && au_data[i+2] == 0x00 && au_data[i+3] == 0x01) {
                        has_start_code = true;
                        break;
                    }
                }
            }
        }
        
        if (!has_start_code) {
            LOG_WARN("Client " << client_id << ": Data doesn't contain H.264 start code, may be invalid");
            // 记录前几个字节用于调试
            std::stringstream ss;
            ss << "First 16 bytes: ";
            for (size_t i = 0; i < std::min<size_t>(au_data.size(), 16); i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)au_data[i] << " ";
            }
            LOG_DEBUG(ss.str());
            return {};
        }
        
        // 先将Annex-B转换为AVCC格式
        std::vector<uint8_t> avcc_data = convertAnnexBToAVCC(au_data);
        
        if (avcc_data.empty()) {
            LOG_ERROR("Client " << client_id << ": Failed to convert Annex-B to AVCC");
            return {};
        }
        
        LOG_DEBUG("Client " << client_id << ": AVCC data size: " << avcc_data.size());
        
        // Log first 20 bytes of AVCC data for debugging
        if (avcc_data.size() >= 20) {
            std::stringstream ss;
            ss << "Client " << client_id << ": First 20 bytes of AVCC: ";
            for (size_t i = 0; i < 20; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)avcc_data[i] << " ";
            }
            LOG_DEBUG(ss.str());
        }
        
        std::vector<uint8_t> flv_tag;
        flv_tag.reserve(avcc_data.size() + 20);
        
        // FLV Tag Header (11 bytes)
        flv_tag.push_back(FLV_TAG_TYPE_VIDEO);  // Tag type
        
        // Data size (3 bytes, big-endian)
        uint32_t data_size = (uint32_t)avcc_data.size() + 5;  // +5 for video tag data header
        writeUint24BE(flv_tag, data_size);
        
        LOG_DEBUG("Client " << client_id << ": DataSize = " << data_size 
                 << " (avcc_size=" << avcc_data.size() << " + 5)");
        
        // Verify DataSize bytes written to buffer
        if (flv_tag.size() >= 4) {
            uint32_t written_datasize = (flv_tag[1] << 16) | (flv_tag[2] << 8) | flv_tag[3];
            LOG_DEBUG("Client " << client_id << ": Written DataSize bytes: 0x" 
                     << std::hex << std::setfill('0') << std::setw(2) << (int)flv_tag[1]
                     << std::setw(2) << (int)flv_tag[2] 
                     << std::setw(2) << (int)flv_tag[3]
                     << std::dec << " = " << written_datasize);
            if (written_datasize != data_size) {
                LOG_ERROR("Client " << client_id << ": DataSize mismatch! Calculated=" 
                         << data_size << ", Written=" << written_datasize);
            }
        }
        
        // Timestamp (3 bytes + 1 extended, big-endian)
        // FLV Timestamp: Lower 24 bits
        writeUint24BE(flv_tag, timestamp & 0xFFFFFF);
        // FLV Timestamp: Upper 8 bits (Extended)
        flv_tag.push_back((timestamp >> 24) & 0xFF); 
        
        // Stream ID (3 bytes, always 0)
        writeUint24BE(flv_tag, 0);
        
        // Video Tag Data
        // Frame type (4 bits) + Codec ID (4 bits)
        // Frame type: 1 = keyframe, 2 = inter frame
        // Codec ID: 7 = AVC (H.264)
        uint8_t frame_type = is_keyframe ? 0x17 : 0x27;  // 1:keyframe/2:inter + 7:AVC
        flv_tag.push_back(frame_type);
        
        // AVC packet type: 1 = NALU
        flv_tag.push_back(0x01);
        
        // Composition time (3 bytes, 0 for now - assuming DTS=PTS)
        writeUint24BE(flv_tag, 0);
        
        // H.264 data in AVCC format (length-prefixed NAL units)
        flv_tag.insert(flv_tag.end(), avcc_data.begin(), avcc_data.end());
        
        // Previous tag size (4 bytes, big-endian)
        uint32_t prev_tag_size = (uint32_t)flv_tag.size();
        writeUint32BE(flv_tag, prev_tag_size);
        
        LOG_DEBUG("Client " << client_id << ": FLV tag created, size: " << flv_tag.size() << " bytes");
        return flv_tag;
    }
    
    // 将 H.264 数据封装为 FLV video tag
    // 假设 h264_data 已经是单帧数据
    std::vector<uint8_t> createFlvVideoTag(const std::vector<uint8_t>& h264_data, uint32_t timestamp, bool is_keyframe, int client_id) {
        return createFlvVideoTagFromAU(h264_data, timestamp, is_keyframe, client_id);
    }
    
    // 验证数据是否为有效的H.264数据
    bool validateH264Data(const std::vector<uint8_t>& data, int client_id = -1) {
        if (data.empty()) {
            if (client_id >= 0) LOG_DEBUG("Client " << client_id << ": Empty data");
            return false;
        }
        
        // 检查是否有有效的start code
        bool has_start_code = false;
        for (size_t i = 0; i < std::min<size_t>(data.size(), 32); i++) {
            if (i + 3 < data.size()) {
                if (data[i] == 0x00 && data[i+1] == 0x00) {
                    if (data[i+2] == 0x01) {
                        has_start_code = true;
                        break;
                    }
                    if (i + 4 < data.size() && data[i+2] == 0x00 && data[i+3] == 0x01) {
                        has_start_code = true;
                        break;
                    }
                }
            }
        }
        
        if (!has_start_code) {
            if (client_id >= 0) {
                LOG_WARN("Client " << client_id << ": Data doesn't contain H.264 start code");
            }
            return false;
        }
        
        // 检查NAL单元类型是否有效
        size_t start_pos = 0;
        while (start_pos < data.size()) {
            if (start_pos + 4 >= data.size()) break;
            
            if (data[start_pos] == 0x00 && data[start_pos+1] == 0x00) {
                size_t nal_start = 0;
                if (data[start_pos+2] == 0x01) {
                    nal_start = start_pos + 3;
                } else if (start_pos + 3 < data.size() && 
                          data[start_pos+2] == 0x00 && data[start_pos+3] == 0x01) {
                    nal_start = start_pos + 4;
                } else {
                    start_pos++;
                    continue;
                }
                
                if (nal_start < data.size()) {
                    uint8_t nal_type = data[nal_start] & 0x1F;
                    // 合法的视频NAL类型
                    bool valid_nal_type = (nal_type >= 1 && nal_type <= 5) ||  // 1-5: 视频切片
                                          nal_type == 6 ||  // SEI
                                          nal_type == 7 ||  // SPS
                                          nal_type == 8 ||  // PPS
                                          nal_type == 9;    // 访问单元分隔符
                    
                    if (!valid_nal_type && client_id >= 0) {
                        LOG_WARN("Client " << client_id << ": Invalid NAL type: " << (int)nal_type);
                    }
                    
                    return valid_nal_type;
                }
            }
            start_pos++;
        }
        
        return false;
    }
}

HttpFlvStreamer::HttpFlvStreamer(int port) : port_(port) {
    LOG_INFO("HttpFlvStreamer created on port " << port_);
}

HttpFlvStreamer::~HttpFlvStreamer() {
    Stop();
}

bool HttpFlvStreamer::Start(std::shared_ptr<IHttFlvStreamHandler> handler) {
    if (running_) {
        LOG_WARN("FLV streamer already running");
        return true;
    }
    
    handler_ = handler;
    server_ = std::make_unique<httplib::Server>();
    
    // 设置服务器参数
    server_->set_keep_alive_max_count(100);
    server_->set_payload_max_length(1024 * 1024 * 10); // 10MB最大负载
    
    setup_routes();
    
    server_thread_ = std::thread([this]() {
        LOG_INFO("Starting FLV HTTP server on port " << port_);
        running_ = true;
        
        if (!server_->listen("0.0.0.0", port_)) {
            LOG_ERROR("Failed to start FLV HTTP server on port " << port_);
            running_ = false;
        }
    });
    
    // 等待服务器启动
    int retry = 0;
    while (!running_ && retry < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        retry++;
    }
    return running_;
}

void HttpFlvStreamer::Stop() {
    if (!running_) return;
    
    LOG_INFO("Stopping FLV HTTP server...");
    running_ = false;
    
    if (server_) {
        server_->stop();
    }
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    LOG_INFO("FLV HTTP server stopped");
}

void HttpFlvStreamer::setup_routes() {
    if (!server_) return;
    
    // FLV流端点
    server_->Get("/live", [this](const httplib::Request& req, httplib::Response& res) {
        if (!handler_) {
            res.status = 500;
            res.set_content("Server error: No stream handler", "text/plain");
            return;
        }
        
        LOG_INFO("New FLV client connected from " << req.remote_addr);
        
        // 注册客户端
        int client_id = handler_->register_flv_client();
        if (client_id < 0) {
            res.status = 503;
            res.set_content("Server busy, cannot accept more clients", "text/plain");
            return;
        }
        
        LOG_INFO("Client registered with ID: " << client_id);
        
        // 设置响应头
        res.set_header("Connection", "keep-alive");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "video/x-flv");
        
        // 获取编码器配置
        int width = 0, height = 0, fps = 30;
        handler_->get_encoder_config(width, height, fps);
        LOG_INFO("Client " << client_id << ": Stream config - " << width << "x" << height << "@" << fps << "fps");
        
        // 使用set_content_provider进行流式传输
        res.set_content_provider(
            "video/x-flv",
            [this, client_id, width, height, fps](size_t offset, httplib::DataSink& sink) {
                // 发送FLV头 (仅在第一次调用时)
                if (offset == 0) {
                    std::vector<uint8_t> flv_header = {
                        'F', 'L', 'V',  // Signature
                        0x01,           // Version 1
                        0x01,           // Flags: 0x01 = video only
                        0x00, 0x00, 0x00, 0x09, // Header size
                        0x00, 0x00, 0x00, 0x00  // Previous tag size
                    };
                    
                    if (!sink.write(reinterpret_cast<char*>(flv_header.data()), flv_header.size())) {
                        LOG_WARN("Client " << client_id << ": Failed to write FLV header");
                        handler_->unregister_flv_client(client_id);
                        return false;
                    }
                    LOG_INFO("Client " << client_id << ": Sent FLV header");
                }
                
                int64_t last_activity = av_gettime() / 1000;
                int64_t start_time = av_gettime() / 1000;
                int64_t frame_start_time = start_time;
                int frame_count = 0;
                int keyframe_count = 0;
                size_t total_bytes_sent = 0;
                bool first_keyframe_sent = false;
                std::vector<uint8_t> sps_data, pps_data;
                bool got_sps_pps = false;
                
                LOG_INFO("Client " << client_id << ": Waiting for first keyframe...");
                
                // 尝试从handler获取SPS/PPS
                got_sps_pps = handler_->get_sps_pps_data(sps_data, pps_data);
                if (got_sps_pps && !sps_data.empty() && !pps_data.empty()) {
                    LOG_INFO("Client " << client_id << ": Got SPS/PPS from handler");
                } else {
                    LOG_INFO("Client " << client_id << ": Will extract SPS/PPS from stream");
                }
                
                while (sink.is_writable() && running_) {
                    // 检查客户端是否超时（30秒无数据）
                    int64_t now = av_gettime() / 1000;
                    if (now - last_activity > 30000) {
                        LOG_WARN("Client " << client_id << ": Timeout (30 seconds no data)");
                        break;
                    }
                    
                    std::vector<uint8_t> h264_data;
                    bool got_data = handler_->get_flv_stream_data(h264_data, client_id);
                    
                    if (got_data && !h264_data.empty()) {
                        // 验证数据是否为有效的H.264视频数据
                        if (!validateH264Data(h264_data, client_id)) {
                            LOG_WARN("Client " << client_id << ": Invalid H.264 data received, discarding");
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            continue;
                        }
                        
                        bool is_key = isKeyFrame(h264_data);
                        
                        // 等待第一个关键帧
                        if (!first_keyframe_sent) {
                            if (!is_key) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                continue;
                            }
                            
                            LOG_INFO("Client " << client_id << ": First keyframe received");
                            
                            // 如果还没有SPS/PPS，尝试从当前流中提取
                            if ((sps_data.empty() || pps_data.empty())) {
                                LOG_DEBUG("Client " << client_id << ": Extracting SPS/PPS from stream...");
                                SPSPPSData extracted = extractSPSPPS(h264_data);
                                sps_data = extracted.sps;
                                pps_data = extracted.pps;
                                
                                if (!sps_data.empty() && !pps_data.empty()) {
                                    LOG_INFO("Client " << client_id << ": Extracted SPS/PPS from stream");
                                    got_sps_pps = true;
                                }
                            }

                            if (got_sps_pps && !sps_data.empty() && !pps_data.empty()) {
                                std::vector<uint8_t> avc_config = createAVCDecoderConfigRecord(sps_data, pps_data);
                                if (!avc_config.empty()) {
                                    if (!sink.write(reinterpret_cast<char*>(avc_config.data()), avc_config.size())) {
                                        LOG_WARN("Client " << client_id << ": Failed to write AVC config");
                                        break;
                                    }
                                    LOG_INFO("Client " << client_id << ": Sent Sequence Header (SPS/PPS), size: " << avc_config.size());
                                    total_bytes_sent += avc_config.size();
                                } else {
                                    LOG_ERROR("Client " << client_id << ": Failed to create AVC config record");
                                }
                            } else {
                                LOG_ERROR("Client " << client_id << ": Cannot get valid SPS/PPS, stream may be unplayable");
                            }
                            
                            first_keyframe_sent = true;
                            start_time = av_gettime() / 1000; // 重置时间戳起点
                            frame_start_time = start_time;
                            LOG_INFO("Client " << client_id << ": Stream started successfully");
                        }
                        
                        if (is_key) keyframe_count++;
                        
                        // 计算时间戳（基于实际帧间隔）
                        int64_t current_time = av_gettime() / 1000;
                        uint32_t timestamp = 0;
                        
                        if (frame_count > 0 && fps > 0) {
                            // 基于帧率计算理论时间戳
                            timestamp = static_cast<uint32_t>((frame_count * 1000) / fps);
                        } else {
                            // 使用实际经过时间
                            timestamp = static_cast<uint32_t>(current_time - start_time);
                        }
                        
                        // 创建FLV标签
                        std::vector<uint8_t> flv_tag = createFlvVideoTag(h264_data, timestamp, is_key, client_id);
                        
                        if (!flv_tag.empty()) {
                            // 写入数据
                            if (!sink.write(reinterpret_cast<char*>(flv_tag.data()), flv_tag.size())) {
                                LOG_WARN("Client " << client_id << ": Write failed (disconnected)");
                                break;
                            }
                            
                            frame_count++;
                            total_bytes_sent += flv_tag.size();
                            last_activity = current_time;
                            
                            // 控制帧率
                            if (fps > 0) {
                                int64_t frame_duration = 1000 / fps;
                                int64_t actual_duration = current_time - frame_start_time;
                                if (actual_duration < frame_duration) {
                                    std::this_thread::sleep_for(
                                        std::chrono::milliseconds(frame_duration - actual_duration));
                                }
                            }
                            frame_start_time = av_gettime() / 1000;
                            
                            // 定期输出统计信息
                            if (frame_count % 100 == 0) {
                                LOG_INFO("Client " << client_id << " stats: frames=" << frame_count 
                                          << ", keyframes=" << keyframe_count 
                                          << ", bytes=" << total_bytes_sent
                                          << ", timestamp=" << timestamp);
                            }
                        } else {
                            LOG_WARN("Client " << client_id << ": Failed to create FLV tag");
                        }
                    } else {
                        // 无数据时短暂休眠，避免空转CPU
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                
                handler_->unregister_flv_client(client_id);
                LOG_INFO("FLV stream ended for client " << client_id 
                         << ", total frames: " << frame_count 
                         << ", total bytes: " << total_bytes_sent);
                return true;
            }
        );
    });
    
    // 状态端点
    server_->Get("/status", [this](const httplib::Request&, httplib::Response& res) {
        std::stringstream ss;
        ss << "<html><body>";
        ss << "<h1>FLV Stream Server Status</h1>";
        ss << "<p>Port: " << port_ << "</p>";
        ss << "<p>Running: " << (running_ ? "Yes" : "No") << "</p>";
        ss << "<p>Handler: " << (handler_ ? "Available" : "Not available") << "</p>";
        ss << "</body></html>";
        res.set_content(ss.str(), "text/html");
    });
    
    // 健康检查端点
    server_->Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        if (running_ && handler_) {
            res.set_content("OK", "text/plain");
        } else {
            res.status = 503;
            res.set_content("Service Unavailable", "text/plain");
        }
    });
}
