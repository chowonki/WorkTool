#pragma once
#include "Ui.h"
#include <set>

namespace worktool
{
    class Transfer
    {
    public:
        explicit Transfer(Ui& ui);
        UIElement View() const { return view_; }
        bool Busy() const { return busy_; }
        bool Unsaved() const { return rows_.Size() && rows_.Stringify() != saved_; }
    private:
        struct Thread { std::wstring id, imported; TextBox input{nullptr}; };
        void Show(int step);
        void AddThread();
        void RenderThreads();
        void Extract();
        void Review();
        void Preview();
        void Recheck();
        void UpdateDownload();
        void Split();
        void Guard(Action action);
        fire_and_forget Edit(uint32_t index, bool add);
        fire_and_forget Bulk();
        fire_and_forget DeleteSelected();
        fire_and_forget Reset();
        fire_and_forget Download();

        Ui& ui_;
        StackPanel view_ = Column(16);
        StackPanel inputStep_ = Column();
        StackPanel reviewStep_ = Column();
        StackPanel downloadStep_ = Column();
        StackPanel threadList_ = Column(12);
        StackPanel reviewTable_ = Column(3);
        StackPanel previewTable_ = Column(3);
        TextBlock status_ = Label(L"");
        TextBlock summary_ = Label(L"");
        TextBlock previewSummary_ = Label(L"");
        ComboBox filter_;
        TextBox search_ = Input(L"검토 검색");
        TextBox cms_ = Input(L"CMS 코드 (선택)");
        TextBox comments_ = Input(L"송금 코멘트", {}, true);
        CheckBox acknowledge_;
        Button download_{nullptr};
        std::vector<Thread> threads_;
        std::set<std::wstring> selected_;
        JsonArray banks_, rows_, checks_;
        hstring saved_;
        uint32_t errors_{}, warnings_{};
        bool busy_{};
    };
}
