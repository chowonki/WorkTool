#include "pch.h"
#include "MainWindow.h"

// 개발용 화면 검증: 가상 자료만 써요. 대화 상자나 바깥 서비스는 쓰지 않아요.
//   WorkTool.exe --smoke-test <결과 폴더> <가상 자료 폴더>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;

namespace wt
{
    static std::string JsonReport(bool passed, std::vector<std::wstring> const& checks, std::wstring const& error)
    {
        std::string text = std::string("{\n  \"passed\": ") + (passed ? "true" : "false") + ",\n  \"checks\": [";
        for (size_t i = 0; i < checks.size(); i++) text += (i ? ",\n    " : "\n    ") + JsString(checks[i]);
        text += "\n  ]";
        if (!passed) text += ",\n  \"error\": " + JsString(error);
        return text + "\n}\n";
    }

    Async MainWindow::SetTextAndWait(TextBox box, std::wstring value)
    {
        box.Text(value);
        co_await Delay(150);
    }

    // 검증용 화면 사진: 창 내용을 그대로 PNG로 남겨요. Mica는 시스템이 그려서 사진에는 단색(#F3F3F3/#202020)으로 대신 깔아요.
    Async MainWindow::Screenshot(std::wstring name)
    {
        if (!smokeDirectory) co_return;
        notice.IsOpen(false);
        auto dark = root.ActualTheme() == ElementTheme::Dark;
        uint8_t shade = dark ? 0x20 : 0xF3;
        root.Background(Media::SolidColorBrush(Windows::UI::Color{ 255, shade, shade, shade }));
        co_await Delay(500);
        Media::Imaging::RenderTargetBitmap bitmap;
        co_await bitmap.RenderAsync(root);
        auto pixels = co_await bitmap.GetPixelsAsync();
        InMemoryRandomAccessStream stream;
        auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
        encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, static_cast<uint32_t>(bitmap.PixelWidth()),
            static_cast<uint32_t>(bitmap.PixelHeight()), 96, 96, array_view<uint8_t const>(pixels.data(), pixels.Length()));
        co_await encoder.FlushAsync();
        auto size = static_cast<uint32_t>(stream.Size());
        DataReader reader(stream.GetInputStreamAt(0));
        co_await reader.LoadAsync(size);
        std::string bytes(size, '\0');
        reader.ReadBytes(array_view<uint8_t>(reinterpret_cast<uint8_t*>(bytes.data()), size));
        WriteFile(*smokeDirectory / L"screens" / (name + L".png"), bytes);
        root.ClearValue(Controls::Panel::BackgroundProperty());
    }

    Async MainWindow::SmokeTest(std::wstring output, std::wstring fixtures)
    {
        std::vector<std::wstring> checks;
        auto check = [&checks](bool passed, std::wstring label)
        {
            if (!passed) throw AppError{ label };
            checks.push_back(std::move(label));
        };
        auto filesMatching = [this](std::wstring_view prefix, std::wstring_view extension, uintmax_t minimum)
        {
            int count = 0;
            std::error_code error;
            for (auto const& entry : std::filesystem::directory_iterator(*smokeDirectory, error))
            {
                auto name = entry.path().filename().wstring();
                if (entry.is_regular_file() && name.starts_with(prefix) && name.ends_with(extension) && entry.file_size() >= minimum) count++;
            }
            return count;
        };
        auto shownTransfers = [this] { return std::count_if(transferViews.begin(), transferViews.end(), [](auto const& view) { return IsShown(view.card); }); };
        auto lines = [](hstring const& text) { return NormalizeLines(text); };
        std::filesystem::path fixtureDirectory(fixtures);
        smokeDirectory = std::filesystem::absolute(output);
        std::error_code ignored;
        std::filesystem::create_directories(*smokeDirectory, ignored);
        std::wstring failure;
        try
        {
            // 화면 사진이 같은 크기로 나오도록 1440×900(배율 반영)으로 맞춰요.
            auto scale = root.XamlRoot() ? root.XamlRoot().RasterizationScale() : 1.0;
            appWindow.Resize({ static_cast<int>(1440 * scale), static_cast<int>(900 * scale) });
            co_await Delay(400);
            co_await Screenshot(L"home");
            Go(L"transfer");
            threadBoxes[0].Text(L"123가4567\r국민은행\r001234567890\r3만5천원");
            ExtractTransfer();
            check(transfers.size() == 1 && Str(transfers[0], L"amount") == L"35000" && Str(transfers[0], L"plate") == L"123가4567", L"WinUI multiline input extracts correctly");
            ExtractTransfer();
            check(transfers.size() == 1, L"Repeated extraction does not duplicate transfer");
            Step(3);
            check(download.IsEnabled(), L"Valid transfer enables download");
            Put(transfers[0], L"amount", L"0");
            ValidateTransfers();
            check(!download.IsEnabled(), L"Zero amount blocks download");
            Put(transfers[0], L"amount", L"100000000");
            RenderTransfers();
            check(!download.IsEnabled(), L"Large amount requires review");
            acknowledge.IsChecked(true);
            check(download.IsEnabled(), L"Review enables download");
            Put(transfers[0], L"inMemo", L"수정");
            ValidateTransfers();
            check(!IsOn(acknowledge) && !download.IsEnabled(), L"Editing invalidates review");
            Put(transfers[0], L"amount", L"35000");
            RenderTransfers();
            Step(3);
            co_await SaveTransfer();
            check(filesMatching(L"하나은행", L".xlsx", 1) == 1, L"Native transfer export writes Excel");
            auto secondThread = AddThreadInput();
            secondThread.Text(L"45나6789\r(정산 계좌)\r신한은행\r987654321012\r예금주: 테스트\r2만원");
            ExtractTransfer();
            check(transfers.size() == 2 && Str(transfers[1], L"holder") == L"테스트" && Str(transfers[1], L"label") == L"정산 계좌",
                L"Second Slack input preserves holder and remittance context");
            ShowTransferDetail(transfers[1]);
            co_await Screenshot(L"transfer-step2");
            check(NormalizeTransferValue(L"amount", L"3만 5천원") == L"35000" && NormalizeTransferValue(L"account", L"001-234 567890") == L"001234567890",
                L"Manual amount and account edits follow web formatting");
            auto malformed = NormalizeTransferValue(L"amount", L"35,00원");
            auto malformedRow = Clone(transfers[0]);
            Put(malformedRow, L"amount", malformed);
            check(malformed == L"35,00원" && Str(rules.Check({ malformedRow })[0], L"state") == L"bad" && NormalizeTransferValue(L"amount", L"35,000.00원") == L"35000",
                L"Malformed amounts remain invalid while standard formats normalize");
            co_await SetTextAndWait(transferSearch, L"987654321012");
            check(shownTransfers() == 1, L"Transfer search filters review rows");
            co_await SetTextAndWait(transferSearch, L"");
            Go(L"dispatch");
            check(results.empty() && IsShown(dispatchEmpty) && IsShown(dispatchInputPanel) && navCounts.at(L"d:all").Text().empty(),
                L"Dispatch starts with the input panel, an empty table and no navigation counts");
            co_await SetTextAndWait(vehicles, L"123가4567\r\n123가4567\r\n45나6789\r\n확인 필요");
            check(vehicleCount.Text() == L"2대 인식 · 중복 1줄 · 확인 필요 1줄", L"Vehicle input counts unique valid plates (" + std::wstring(vehicleCount.Text()) + L")");
            co_await SetTextAndWait(vehicles, L"");
            check(vehicleCount.Text() == L"0대 인식", L"Clearing vehicle input resets the count");
            std::wstring const pasted = L"\n\n12가3456\n\n  \n34나5678\r\n\r\n56다7890\n\n";
            check(RemoveBlankLines(pasted) == L"12가3456\n34나5678\n56다7890\n", L"Blank-line cleanup keeps one trailing newline");
            co_await SetTextAndWait(vehicles, pasted);
            co_await Delay(200);
            check(lines(vehicles.Text()) == L"12가3456\n34나5678\n56다7890\n", L"Pasting removes blank lines (" + Replace(lines(vehicles.Text()), L"\n", L"⏎") + L")");
            co_await SetTextAndWait(vehicles, std::wstring(vehicles.Text()) + L"\r");
            co_await Delay(200);
            check(lines(vehicles.Text()) == L"12가3456\n34나5678\n56다7890\n\n", L"Typing a single Enter keeps the new empty line");
            co_await SetTextAndWait(vehicles, L"");
            auto vehicleList = ToWide(ReadFile(fixtureDirectory / L"차량목록.txt").value_or(""));
            co_await SetTextAndWait(vehicles, vehicleList);
            for (auto const& entry : std::filesystem::directory_iterator(fixtureDirectory))
                if (entry.path().extension() == L".xlsx" && entry.path().filename() != L"encrypted.xlsx")
                    files.push_back(LoadVendorFile(entry.path(), entry.path().filename().wstring()));
            // 암호 엑셀(가상 차량 한 건, 암호 fixture-password)이 있으면 암호 풀기도 확인해요.
            if (auto locked = ReadFile(fixtureDirectory / L"encrypted.xlsx"))
            {
                auto wrongRejected = false;
                try { rules.ReadVendorFile(*locked, L"encrypted.xlsx", std::wstring(L"wrong-password")); }
                catch (hresult_error const&) { wrongRejected = true; }
                auto opened = rules.ReadVendorFile(*locked, L"encrypted.xlsx", std::wstring(L"fixture-password"));
                check(Rules::Inspect(*locked) == Rules::ExcelKind::Encrypted && wrongRejected && Arr(opened, L"records").Size() > 0,
                    L"Password-protected Excel opens only with the right password (" + std::to_wstring(Arr(opened, L"records").Size()) + L" records)");
            }
            RenderDispatchFiles();
            check(files.size() >= 3, L"Vendor Excel files are read by the embedded rules engine (" + std::to_wstring(files.size()) + L")");
            co_await SetTextAndWait(vehicles, std::wstring(vehicles.Text()) + L"\r\r");
            QueryDispatch();
            check(!results.empty(), L"Native dispatch renders synthetic vendor files");
            co_await Delay(200);
            auto queried = lines(vehicles.Text());
            check(!results.empty() && queried.find(L"\n\n") == std::wstring::npos && !queried.starts_with(L'\n') && !queried.ends_with(L'\n'),
                L"Lookup trims blank lines and edge newlines without clearing results (results=" + std::to_wstring(results.size()) + L")");
            check(!IsShown(dispatchEmpty) && dispatchList.Children().Size() > 0 && !navCounts.at(L"d:all").Text().empty() && currentNav == L"d:all",
                L"Successful lookup fills the table and the navigation counts");
            co_await Screenshot(L"dispatch");
            Select(L"d:confirmed");
            auto confirmed = VisibleResults();
            check(dispatchFilter == L"confirmed" && std::all_of(confirmed.begin(), confirmed.end(), [](auto const& row) { return Str(row, L"status") == L"confirmed"; }),
                L"Navigation sub-item filters the table");
            Select(L"d:all");
            auto firstVehicleId = Str(results[0], L"id");
            selected.insert(firstVehicleId);
            co_await SetTextAndWait(search, Str(results[0], L"plate"));
            check(VisibleResults().size() == 1 && selected.contains(firstVehicleId), L"Search keeps checked rows");
            co_await SetTextAndWait(search, L"");
            selected.clear();
            for (auto& row : results)
                for (std::wstring field : { L"schedule", L"fee" })
                    if (Flag(row, field + L"Conflict")) row = rules.Choose(row, field, Str(Objects(Arr(row, field + L"Candidates")).at(0), L"id"));
            RenderDispatch();
            auto allMatches = static_cast<int>(VisibleResults().size());
            auto savedPageSize = dispatchPageSize;
            SetDispatchPageSize(10);
            check(static_cast<int>(dispatchList.Children().Size()) == std::min(10, allMatches) && dispatchPage == 0, L"Dispatch shows one page of the chosen size");
            if (allMatches > 10)
            {
                ChangeDispatchPage(1);
                check(static_cast<int>(dispatchList.Children().Size()) == std::min(10, allMatches - 10)
                    && std::wstring(dispatchSummary.Text()).find(L"11–") != std::wstring::npos, L"Dispatch moves to the next page");
            }
            SetDispatchPageSize(savedPageSize == 10 ? 30 : savedPageSize);
            check(dispatchPageSize != 10 && static_cast<int>(dispatchList.Children().Size()) == std::min(dispatchPageSize, allMatches), L"Dispatch page size can be changed back");
            dispatchVendor = L"한빛탁송";
            auto byVendor = VisibleResults();
            check(static_cast<int>(byVendor.size()) < allMatches && std::all_of(byVendor.begin(), byVendor.end(), [](auto const& row) { return Str(row, L"vendor") == L"한빛탁송"; }),
                L"Dispatch filters by vendor");
            dispatchVendor.clear();
            dispatchSort = L"amount";
            dispatchDescending = true;
            auto sorted = VisibleResults();
            check(Str(sorted.front(), L"amount") == L"20000" && Str(sorted.back(), L"amount").empty(), L"Dispatch sorts numeric values with empty values last");
            dispatchSort.clear();
            dispatchDescending = false;
            co_await ExportDispatch(L".xlsx");
            co_await ExportDispatch(L".txt");
            co_await CaptureDispatch(false);
            co_await CaptureDispatch(false, true);
            check(filesMatching(L"탁송체크", L".xlsx", 500) == 1 && filesMatching(L"탁송체크", L".txt", 10) == 1, L"Dispatch export writes Excel and text");
            check(filesMatching(L"", L".png", 1000) >= 1, L"Native PNG capture writes image");
            check(filesMatching(L"", L".pdf", 1000) >= 1, L"Native PDF capture writes document");
            co_await SetTextAndWait(vehicles, L"123가4567");
            check(results.empty() && IsShown(dispatchEmpty) && navCounts.at(L"d:all").Text().empty(), L"Editing source invalidates prior dispatch results");
            co_await SetTextAndWait(vehicles, vehicleList);
            QueryDispatch();
            dispatchVendor = L"한빛탁송";
            ShowSlackPanel(true);
            check(SlackPanelOpen(), L"Slack reply panel opens in the right pane");
            co_await SetTextAndWait(slackReplies, L"08구8701 - X\n77다1234 - 확인 중");
            ApplySlackReplies();
            auto replies = VisibleResults();
            auto has = [&replies](wchar_t const* plate, wchar_t const* money)
            {
                return std::any_of(replies.begin(), replies.end(), [&](auto const& row) { return Str(row, L"plate") == plate && Money(row, L"amount") == money; });
            };
            check(dispatchFilter == L"slack-replies" && IsShown(navItems.at(L"d:slack-replies")) && std::wstring(slackReplyReport.Text()).starts_with(L"2대 반영")
                && replies.size() == 2 && has(L"08구8701", L"0원") && has(L"77다1234", L"확인 중"),
                L"Applying replies opens the results table and shows the updated fuel cells (" + std::wstring(slackReplyReport.Text()) + L")");
            root.RequestedTheme(ElementTheme::Dark);
            check(root.RequestedTheme() == ElementTheme::Dark, L"Dark theme applies");
            co_await Screenshot(L"dispatch-dark");
            Go(L"transfer");
            Step(2);
            co_await Screenshot(L"transfer-step2-dark");
            Go(L"dispatch");
            co_await SetTextAndWait(vehicles, std::wstring(vehicles.Text()) + L"\r12가3456");
            check(results.empty() && dispatchFilter == L"all" && !IsShown(navItems.at(L"d:slack-replies")), L"Clearing results leaves the hidden 슬랙 회신 반영 view");
            root.RequestedTheme(ElementTheme::Default);
        }
        catch (hresult_error const& error) { failure = error.message(); if (failure.empty()) failure = L"hresult " + std::to_wstring(error.code().value); }
        catch (std::exception const& error) { failure = ToWide(error.what()); }
        WriteFile(*smokeDirectory / L"smoke-result.json", JsonReport(failure.empty(), checks, failure));
        smokeDirectory.reset();
        if (failure.empty()) Notify(L"화면 검증 " + std::to_wstring(checks.size()) + L"개를 통과했어요.");
        else Notify(failure, true);
    }
}
