#pragma once
#include <M5Cardputer.h>
#include <LittleFS.h>
#include <cstring>

// 拼音输入法引擎 — 静态内存版(ESP32 无 PSRAM, 禁用 std::vector/string 动态分配)
// 字典(17.8KB)一次性读入静态缓冲, 内存二分查找。零堆分配, 无 OOM 崩溃风险。
// 字典格式: [uint16 拼音数][每条: uint8 keylen + key + uint16 candcount + cands utf8]
// 记录为变长(非定长), 二分时需顺序推进定位到 mid 条记录。
class PinyinIME {
public:
    PinyinIME() : m_loaded(false), m_composingLen(0), m_candCount(0), m_candPage(0) {
        m_composing[0] = 0;
        m_selected[0] = 0;
    }

    void init(const char* dictPath) {
        m_loaded = loadDict(dictPath);
        clear();
    }

    bool addChar(char c) {
        if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
        if (c >= 'a' && c <= 'z' && m_composingLen < MAX_PINYIN) {
            m_composing[m_composingLen++] = c;
            m_composing[m_composingLen] = 0;
            lookup();
            return true;
        }
        return false;
    }

    bool backspace() {
        if (m_composingLen == 0 && m_candCount > 0) { clear(); return true; }
        if (m_composingLen > 0) {
            m_composing[--m_composingLen] = 0;
            if (m_composingLen > 0) lookup(); else clear();
            return true;
        }
        return false;
    }

    void clear() { m_composingLen = 0; m_composing[0] = 0; m_candCount = 0; m_candPage = 0; }

    bool lookup() {
        m_candCount = 0; m_candPage = 0;
        if (m_composingLen == 0 || !m_loaded) return false;
        int r = findIndex(m_composing, m_composingLen);
        if (r >= 0) {
            m_candCount = recordCandCount(r);
            if (m_candCount > MAX_CAND) m_candCount = MAX_CAND;
            return true;
        }
        return false;
    }

    // 1-based 数字选字; 返回静态缓冲 UTF-8 汉字
    const char* select(int numKey) {
        if (numKey < 1 || numKey > 9) return "";
        int idx = m_candPage * PAGE_SIZE + (numKey - 1);
        if (idx < 0 || idx >= m_candCount) return "";
        int r = findIndex(m_composing, m_composingLen);
        if (r >= 0) {
            const char* cands = recordCands(r);
            memcpy(m_selected, cands + idx * 3, 3);
            m_selected[3] = 0;
        }
        const char* ret = m_selected;
        clear();
        return ret;
    }

    bool isComposing() const { return m_composingLen > 0; }
    bool hasCandidates() const { return m_candCount > 0; }
    bool loaded() const { return m_loaded; }
    int getCandCount() const { return m_candCount; }
    const char* getComposing() const { return m_composing; }
    int getPage() const { return m_candPage; }
    int getTotalPages() const {
        if (m_candCount == 0) return 0;
        return (m_candCount - 1) / PAGE_SIZE + 1;
    }
    bool nextPage() {
        if (m_candCount == 0 || m_candPage >= getTotalPages() - 1) return false;
        m_candPage++; return true;
    }
    bool prevPage() {
        if (m_candCount == 0 || m_candPage <= 0) return false;
        m_candPage--; return true;
    }
    // 当前页第 idx(0-based) 个候选字; 越界返回空串
    const char* candAt(int idx) {
        static char tmp[4];
        tmp[0] = 0;
        int globalIdx = m_candPage * PAGE_SIZE + idx;
        if (globalIdx < 0 || globalIdx >= m_candCount) return tmp;
        int r = findIndex(m_composing, m_composingLen);
        if (r >= 0) {
            memcpy(tmp, recordCands(r) + globalIdx * 3, 3);
            tmp[3] = 0;
        }
        return tmp;
    }

private:
    static constexpr int MAX_PINYIN = 10;
    static constexpr int MAX_CAND = 20;
    static constexpr int PAGE_SIZE = 5;
    static constexpr int MAX_DICT = 20000;

    uint8_t m_dict[MAX_DICT];
    uint32_t m_dictLen;
    uint16_t m_syllCount;
    char m_composing[MAX_PINYIN + 1];
    int m_composingLen;
    int m_candCount;
    int m_candPage;
    char m_selected[4];
    bool m_loaded;

    bool loadDict(const char* path) {
        File f = LittleFS.open(path, "r");
        if (!f) return false;
        m_dictLen = f.read(m_dict, MAX_DICT);
        f.close();
        if (m_dictLen < 2) return false;
        m_syllCount = m_dict[0] | (m_dict[1] << 8);
        return true;
    }

    // 第 idx 条记录 key 的起始偏移(顺序累计: 每条=1+klen+2+ccount*3)
    uint32_t recordOffset(int idx) {
        uint32_t pos = 2;
        for (int i = 0; i < idx && pos < m_dictLen; i++) {
            uint8_t klen = m_dict[pos];
            pos += 1 + klen + 2;               // key + candCount
            uint16_t cc = m_dict[pos - 2] | (m_dict[pos - 1] << 8);
            pos += (uint32_t)cc * 3;           // cands
        }
        return pos;
    }
    uint16_t recordCandCount(int idx) {
        uint32_t pos = recordOffset(idx);
        if (pos + 1 >= m_dictLen) return 0;
        uint8_t klen = m_dict[pos];
        pos += 1 + klen;
        if (pos + 2 > m_dictLen) return 0;
        return m_dict[pos] | (m_dict[pos + 1] << 8);
    }
    const char* recordCands(int idx) {
        uint32_t pos = recordOffset(idx);
        uint8_t klen = m_dict[pos];
        pos += 1 + klen + 2;                   // skip key + candCount
        return (const char*)(m_dict + pos);
    }

    // 二分查找, 返回记录索引; -1 = 未找到
    int findIndex(const char* key, int keyLen) {
        int lo = 0, hi = m_syllCount;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            uint32_t pos = recordOffset(mid);
            uint8_t klen = m_dict[pos];
            const char* k = (const char*)(m_dict + pos + 1);
            int n = keyLen < (int)klen ? keyLen : (int)klen;
            int cmp = strncmp(k, key, n);
            if (cmp == 0) {
                if (keyLen == (int)klen) return mid;
                cmp = keyLen < (int)klen ? -1 : 1;
            }
            if (cmp < 0) lo = mid + 1;
            else hi = mid;
        }
        return -1;
    }
};

extern PinyinIME pinyinIME;
