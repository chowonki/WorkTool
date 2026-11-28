#pragma once
#include "Rules.h"

namespace worktool
{
    Bytes CapturePng(JsonArray const& rows, std::wstring_view scope);
    Bytes CapturePdf(JsonArray const& rows, std::wstring_view scope);
}
