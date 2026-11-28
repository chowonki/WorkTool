#pragma once
#include "Ui.h"
#include <set>

namespace worktool
{
    class Dispatch
    {
    public:
        explicit Dispatch(Ui& ui);
        UIElement View() const { return view_; }
        bool Busy() const { return busy_; }
    private:
        void RefreshFiles();
        void Lookup();
        void RefreshResults();
        void ApplyReply();
        void CopyPlates();
        void Clear();
        void Guard(Action action);
        void SaveMarks();
        void CopyCell(std::wstring const& value, std::wstring const& plate);
        void MarkStale();
        JsonArray ExportRows();
        fire_and_forget PickFiles();
        fire_and_forget LoadFiles(std::vector<fs::path> paths);
        fire_and_forget Choose(uint32_t rowIndex, std::string field);
        fire_and_forget Export(std::wstring extension, bool copy = false);
        StackPanel RenderTable(std::vector<uint32_t> const& indexes, bool header);

        Ui& ui_;
        StackPanel view_ = Column(16);
        TextBox vehicles_ = Input(L"차량목록", {}, true);
        TextBox replies_ = Input(L"Slack 회신", {}, true);
        TextBlock status_ = Label(L"");
        TextBlock counts_ = Label(L"");
        TextBlock pageLabel_ = Label(L"");
        StackPanel filesView_ = Column(4);
        StackPanel table_ = Column(1);
        StackPanel results_ = Column();
        ComboBox filter_;
        ComboBox vendor_;
        ComboBox sort_;
        ComboBox scope_;
        TextBox search_ = Input(L"차량번호 검색");
        JsonArray files_;
        JsonArray rows_;
        std::set<std::wstring> selected_, copied_, done_;
        uint32_t page_{};
        bool busy_{};
        bool stale_{};
    };
}
