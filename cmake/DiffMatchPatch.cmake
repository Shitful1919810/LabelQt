function(labelqt_add_diff_match_patch target_name)
    set(dmp_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/diff-match-patch/cpp")
    set(dmp_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/diff-match-patch-qt6")
    file(MAKE_DIRECTORY "${dmp_generated_dir}")

    # The upstream Qt/C++ port targets Qt 4 era APIs. Keep the submodule pristine and generate a
    # small Qt 6 compatible copy in the build tree instead of patching third-party files in place.
    file(READ "${dmp_source_dir}/diff_match_patch.h" dmp_header)
    string(REPLACE
        "#include <QMap>"
        "#include <QMap>\n#include <QRegularExpression>"
        dmp_header
        "${dmp_header}"
    )
    string(REPLACE
        "static QRegExp BLANKLINEEND;"
        "static QRegularExpression BLANKLINEEND;"
        dmp_header
        "${dmp_header}"
    )
    string(REPLACE
        "static QRegExp BLANKLINESTART;"
        "static QRegularExpression BLANKLINESTART;"
        dmp_header
        "${dmp_header}"
    )
    file(WRITE "${dmp_generated_dir}/diff_match_patch.h" "${dmp_header}")

    file(READ "${dmp_source_dir}/diff_match_patch.cpp" dmp_source)
    string(REPLACE
        "prettyText.replace('\\n', L'\\u00b6');"
        "prettyText.replace(QChar('\\n'), QChar(0x00b6));"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "#include <QtCore>\n#include <time.h>"
        "#include <QtCore>\n#ifdef DELETE\n#undef DELETE\n#endif\n#include <time.h>"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "BLANKLINEEND.indexIn(one) != -1"
        "BLANKLINEEND.match(one).hasMatch()"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "BLANKLINESTART.indexIn(two) != -1"
        "BLANKLINESTART.match(two).hasMatch()"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "QRegExp diff_match_patch::BLANKLINEEND = QRegExp(\"\\\\n\\\\r?\\\\n$\");"
        "QRegularExpression diff_match_patch::BLANKLINEEND(QStringLiteral(\"\\\\n\\\\r?\\\\n$\"));"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "QRegExp diff_match_patch::BLANKLINESTART = QRegExp(\"^\\\\r?\\\\n\\\\r?\\\\n\");"
        "QRegularExpression diff_match_patch::BLANKLINESTART(QStringLiteral(\"^\\\\r?\\\\n\\\\r?\\\\n\"));"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE ".toAscii()" ".toLatin1()" dmp_source "${dmp_source}")
    string(REPLACE
        "std::min(loc, text.length())"
        "std::min(loc, static_cast<int>(text.length()))"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "std::min(loc + bin_mid, text.length())"
        "std::min(loc + bin_mid, static_cast<int>(text.length()))"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "std::min(text.length(), patch.start2 + patch.length1 + padding)"
        "std::min(static_cast<int>(text.length()), patch.start2 + patch.length1 + padding)"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "std::min(diff_text.length(),\n              patch_size - patch.length1 - Patch_Margin)"
        "std::min(static_cast<int>(diff_text.length()),\n              patch_size - patch.length1 - Patch_Margin)"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE "QString::SkipEmptyParts" "Qt::SkipEmptyParts" dmp_source "${dmp_source}")
    string(REPLACE
        "QRegExp patchHeader(\"^@@ -(\\\\d+),?(\\\\d*) \\\\+(\\\\d+),?(\\\\d*) @@$\");"
        "QRegularExpression patchHeader(QStringLiteral(\"^@@ -(\\\\d+),?(\\\\d*) \\\\+(\\\\d+),?(\\\\d*) @@$\"));"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE
        "if (!patchHeader.exactMatch(text.front())) {"
        "const QRegularExpressionMatch patchHeaderMatch = patchHeader.match(text.front());\n    if (!patchHeaderMatch.hasMatch()) {"
        dmp_source
        "${dmp_source}"
    )
    string(REPLACE "patchHeader.cap(" "patchHeaderMatch.captured(" dmp_source "${dmp_source}")
    file(WRITE "${dmp_generated_dir}/diff_match_patch.cpp" "${dmp_source}")

    add_library(${target_name} STATIC
        "${dmp_generated_dir}/diff_match_patch.cpp"
        "${dmp_generated_dir}/diff_match_patch.h"
    )
    target_include_directories(${target_name}
        PUBLIC
            "${dmp_generated_dir}"
    )
    target_link_libraries(${target_name} PUBLIC Qt6::Core)
    set_target_properties(${target_name} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()
