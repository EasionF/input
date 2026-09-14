#include "pinyin_engine.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace netroom::engine {

// 虚拟键码（免依赖 windows.h，便于跨工具链单测）
inline constexpr std::uint32_t kVkBack = 0x08;
inline constexpr std::uint32_t kVkEscape = 0x1B;
inline constexpr std::uint32_t kVkSpace = 0x20;

namespace {
using netroom::ipc::CandidateFrame;
using netroom::ipc::CandidateItem;
using netroom::ipc::CandidateKind;

// 小规模词库：音节 -> 候选（按常用度排序）。可被 librime 替换；此处落底。
struct Entry { const char* syllable; const char* words[6]; int n; };
const Entry kDict[] = {
    {"wo", {"我","握","窝","卧"}, 4},
    {"ni", {"你","尼","泥","拟","逆"}, 5},
    {"hao", {"好","号","浩","耗"}, 4},
    {"zhu", {"猪","住","主","煮","筑"}, 5},
    {"shu", {"书","输","树","属"}, 4},
    {"de", {"的","得","地","德"}, 4},
    {"ma", {"马","吗","妈","麻","码"}, 5},
    {"da", {"大","打","嗒","达"}, 4},
    {"guo", {"国","过","果","锅"}, 4},
    {"zhong", {"中","众","重","种"}, 4},
    {"wen", {"文","问","闻","温"}, 4},
    {"shi", {"是","时","十","事"}, 4},
};
constexpr int kDictSize = static_cast<int>(sizeof(kDict)/sizeof(kDict[0]));

bool IsSyllable(const std::string& s) {
    for (int i=0;i<kDictSize;++i)
        if (s == kDict[i].syllable) return true;
    return false;
}
bool IsSyllablePrefix(const std::string& s) {
    if (s.empty()) return false;
    for (int i=0;i<kDictSize;++i){
        const char* sy = kDict[i].syllable;
        if (std::string(sy).compare(0, s.size(), s) == 0) return true;
    }
    return false;
}
const Entry* FindEntry(const std::string& s) {
    for (int i=0;i<kDictSize;++i)
        if (s == kDict[i].syllable) return &kDict[i];
    return nullptr;
}
}  // namespace

void PinyinEngine::Reset() { input_.clear(); }

void PinyinEngine::BuildActiveCandidate(CandidateFrame* out, int status) {
    out->status     = status;
    out->caret      = static_cast<std::uint32_t>(input_.size());
    out->composition = input_;
    out->items.clear();

    if (status == 0 || status == 1) {
        const Entry* e = FindEntry(input_);
        if (e) {
            out->items.reserve(static_cast<std::size_t>(e->n));
            for (int i=0;i<e->n;++i){
                CandidateItem it;
                it.kind  = CandidateKind::Hanzi;
                it.label = static_cast<std::uint16_t>(i+1);
                it.text  = e->words[i];
                out->items.push_back(std::move(it));
            }
        }
    }
}

bool PinyinEngine::ProcessKey(const netroom::ipc::ImeKeyBody& key, CandidateFrame* out) {
    if (!out) return false;
    const std::uint32_t vk = key.vk;
    const bool down = (key.flags & 1u) != 0;
    // 只在按键按下时处理；抬起忽略
    if (!down) return false;

    out->items.clear();
    // 字母：累积拼音
    if (vk >= 'A' && vk <= 'Z') {
        char c = static_cast<char>(std::tolower(static_cast<int>(vk)));
        std::string trial = input_ + c;
        if (IsSyllablePrefix(trial) || IsSyllable(trial)) {
            input_ = trial;
            BuildActiveCandidate(out, 0);
            return true;
        }
        // 非拼音起始：放行
        out->status = 3; out->caret = 0; out->composition.clear(); return false;
    }

    if (vk == kVkBack) {                    // 退格：删一个拼音字母
        if (!input_.empty()) {
            input_.pop_back();
            if (input_.empty()) {
                out->status = 2; out->caret = 0; out->composition.clear();
            } else {
                BuildActiveCandidate(out, 0);
            }
            return true;
        }
        out->status = 3; out->caret = 0; return false;   // 无组合则放行退格
    }

    if (vk == kVkEscape) {                   // 放弃组合
        if (!input_.empty()) {
            out->status = 2; out->caret = 0; out->composition.clear(); input_.clear();
            return true;
        }
        out->status = 3; out->caret = 0; return false;
    }

    // 数字 1..9 与空格：提交对应候选
    int idx = -1;
    if (vk == kVkSpace) idx = 0;
    else if (vk >= '1' && vk <= '9') idx = static_cast<int>(vk - '1');

    if (idx >= 0 && !input_.empty()) {
        const Entry* e = FindEntry(input_);
        if (e && idx < e->n) {
            out->status = 1; out->caret = 0;
            out->composition = e->words[idx];   // 提交文本经由 composition 字段回传
            out->items.clear();
            input_.clear();
            return true;
        }
    }

    // 其余键：组合中则先提交首选再放行其余（净室分类由宿主决定）；这里保守放行
    out->status = 3; out->caret = 0; out->composition.clear();
    return false;
}

}  // namespace netroom::engine