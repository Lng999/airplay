// test_updater.cpp - the pure half of src/ui/updater.* (no network, no window).
//
// The updater runs what it downloads with /SILENT, so the checks that stand between an API
// response and ShellExecute are worth pinning down: the URL allowlist, the .sha256 parsing and
// comparison, and the version/JSON scanners they lean on.
#include "config_store.h"   // winsock2 before windows.h, as in every UI translation unit
#include "updater.h"

#include <cstdio>
#include <string>

namespace {

int g_tests = 0, g_checks = 0, g_failed = 0;
const char* g_name = "";

void beginTest(const char* name) {
    g_name = name;
    ++g_tests;
}

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL  [%s] line %d: %s\n", g_name, line, expr);
    }
}

#define CHECK(e) check((e), #e, __LINE__)

using ui::isAllowedUpdateUrl;

// ---------------------------------------------------------------------------

void testAllowlistAccepts() {
    beginTest("allowlist, accepted");
    CHECK(isAllowedUpdateUrl(L"https://github.com/Lng999/airplay/releases/tag/v0.4.0"));
    CHECK(isAllowedUpdateUrl(
        L"https://github.com/Lng999/airplay/releases/download/v0.4.0/AirPlay-Setup-0.4.0.exe"));
    CHECK(isAllowedUpdateUrl(L"https://objects.githubusercontent.com/github-production-release-asset/1?x=y"));
    CHECK(isAllowedUpdateUrl(L"https://release-assets.githubusercontent.com/github-production/1#f"));
    CHECK(isAllowedUpdateUrl(L"https://github.com"));                 // no path at all
    CHECK(isAllowedUpdateUrl(L"https://github.com?q=1"));
    // scheme and host are case-insensitive
    CHECK(isAllowedUpdateUrl(L"HTTPS://GitHub.COM/Lng999/airplay"));
    CHECK(isAllowedUpdateUrl(L"Https://Objects.GitHubUserContent.com/a"));
}

void testAllowlistScheme() {
    beginTest("allowlist, scheme");
    CHECK(!isAllowedUpdateUrl(L"http://github.com/Lng999/airplay"));
    CHECK(!isAllowedUpdateUrl(L"file:///C:/Windows/System32/calc.exe"));
    CHECK(!isAllowedUpdateUrl(L"file://github.com/x.exe"));
    CHECK(!isAllowedUpdateUrl(L"ftp://github.com/x.exe"));
    CHECK(!isAllowedUpdateUrl(L"C:\\Windows\\System32\\calc.exe"));
    CHECK(!isAllowedUpdateUrl(L"\\\\evil\\share\\setup.exe"));
    CHECK(!isAllowedUpdateUrl(L"//github.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https:/github.com/x"));
    CHECK(!isAllowedUpdateUrl(L" https://github.com/x"));   // leading space
    CHECK(!isAllowedUpdateUrl(L"https://"));
    CHECK(!isAllowedUpdateUrl(L""));
}

void testAllowlistHost() {
    beginTest("allowlist, host");
    CHECK(!isAllowedUpdateUrl(L"https://evil.com/AirPlay-Setup-9.9.9.exe"));
    CHECK(!isAllowedUpdateUrl(L"https://api.github.com/repos/x"));          // not a download host
    CHECK(!isAllowedUpdateUrl(L"https://evilgithub.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://github.com.evil.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://evil.com/github.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://x.github.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://githubusercontent.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://github.com./x"));                   // trailing dot
    CHECK(!isAllowedUpdateUrl(L"https://raw.githubusercontent.com/x"));
}

void testAllowlistAuthorityTricks() {
    beginTest("allowlist, port / userinfo / escapes");
    CHECK(!isAllowedUpdateUrl(L"https://github.com:443/x"));
    CHECK(!isAllowedUpdateUrl(L"https://github.com:8443/x"));
    CHECK(!isAllowedUpdateUrl(L"https://github.com@evil/"));
    CHECK(!isAllowedUpdateUrl(L"https://github.com@evil.com/x.exe"));
    CHECK(!isAllowedUpdateUrl(L"https://user:pass@github.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://evil.com\\@github.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://github.com\\evil.com/x"));
    CHECK(!isAllowedUpdateUrl(L"https://github%2ecom/x"));
    CHECK(!isAllowedUpdateUrl(L"https://[::1]/x"));
    CHECK(!isAllowedUpdateUrl(L"https://140.82.121.4/x"));
    CHECK(!isAllowedUpdateUrl(L"https://git hub.com/x"));
}

// ---------------------------------------------------------------------------

const std::string kEmpty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
const std::string kAbc   = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

void testSha256Hex() {
    beginTest("sha256Hex (FIPS 180-2 vectors)");
    CHECK(ui::sha256Hex("", 0) == kEmpty);
    CHECK(ui::sha256Hex("abc", 3) == kAbc);
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(ui::sha256Hex(two.data(), two.size()) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

void testParseSha256File() {
    beginTest("parseSha256File");
    std::string h;
    CHECK(ui::parseSha256File(kAbc + "  AirPlay-Setup-0.4.0.exe\n", h) && h == kAbc);
    CHECK(ui::parseSha256File(kAbc + " *AirPlay-Setup-0.4.0.exe", h) && h == kAbc);
    CHECK(ui::parseSha256File(kAbc, h) && h == kAbc);                      // bare digest
    CHECK(ui::parseSha256File(kAbc + "\r\n", h) && h == kAbc);             // CRLF
    CHECK(ui::parseSha256File("\xEF\xBB\xBF" + kAbc + "  x.exe", h) && h == kAbc);   // BOM
    CHECK(ui::parseSha256File("  \n" + kAbc + "  x.exe", h) && h == kAbc);
    // upper case in (Get-FileHash prints it that way), lower case out
    std::string upper = kAbc;
    for (char& ch : upper) if (ch >= 'a' && ch <= 'f') ch = static_cast<char>(ch - 32);
    CHECK(ui::parseSha256File(upper + "  x.exe", h) && h == kAbc);

    h = "untouched";
    CHECK(!ui::parseSha256File("", h));
    CHECK(!ui::parseSha256File("not a hash", h));
    CHECK(!ui::parseSha256File(kAbc.substr(0, 63), h));                    // one short
    CHECK(!ui::parseSha256File(kAbc + "0  x.exe", h));                     // 65 digits
    CHECK(!ui::parseSha256File("g" + kAbc.substr(1), h));                  // non-hex digit
    CHECK(!ui::parseSha256File("SHA256 (x.exe) = " + kAbc, h));            // BSD style: refused
    CHECK(!ui::parseSha256File("<html>404</html>", h));
    CHECK(h == "untouched");                                               // left alone on failure
}

void testHexEquals() {
    beginTest("hexEquals");
    std::string upper = kAbc;
    for (char& ch : upper) if (ch >= 'a' && ch <= 'f') ch = static_cast<char>(ch - 32);
    CHECK(ui::hexEquals(kAbc, kAbc));
    CHECK(ui::hexEquals(kAbc, upper));
    CHECK(!ui::hexEquals(kAbc, kEmpty));
    std::string lastDiffers = kAbc;
    lastDiffers.back() = 'e';
    CHECK(!ui::hexEquals(kAbc, lastDiffers));
    CHECK(!ui::hexEquals(kAbc, kAbc.substr(0, 63)));
    CHECK(!ui::hexEquals("", ""));
    CHECK(!ui::hexEquals("zz", "zz"));
}

// A trimmed /releases/latest response: the uploader object carries "url" and a "name" of its
// own, the first asset has a null label, and the body mentions an .exe URL in passing.
const std::string kRelease =
    R"({"url":"https://api.github.com/repos/Lng999/airplay/releases/1",)"
    R"("html_url":"https://github.com/Lng999/airplay/releases/tag/v0.5.0",)"
    R"("tag_name":"v0.5.0","name":"AirPlay Alicisi 0.5.0",)"
    R"("author":{"login":"Lng999","html_url":"https://github.com/Lng999"},)"
    R"("assets":[)"
    R"({"url":"https://api.github.com/a/1","id":1,"uploader":{"name":"evil.exe",)"
    R"("url":"https://evil.com/x.exe","browser_download_url":"https://evil.com/x.exe"},)"
    R"("name":"AirPlay-Setup-0.5.0.exe","label":null,"size":75000000,)"
    R"("browser_download_url":"https://github.com/Lng999/airplay/releases/download/v0.5.0/AirPlay-Setup-0.5.0.exe"},)"
    R"( {"name" : "AirPlay-Setup-0.5.0.exe.sha256", "label":"",)"
    R"( "browser_download_url" : "https://github.com/Lng999/airplay/releases/download/v0.5.0/AirPlay-Setup-0.5.0.exe.sha256"})"
    R"(],"body":"see \"assets\": https://evil.com/y.exe\r\n- line ç"})";

void testParseReleaseAssets() {
    beginTest("parseReleaseAssets");
    const std::vector<ui::ReleaseAsset> a = ui::parseReleaseAssets(kRelease);
    CHECK(a.size() == 2);
    if (a.size() == 2) {
        CHECK(a[0].name == "AirPlay-Setup-0.5.0.exe");
        CHECK(a[0].url ==
              "https://github.com/Lng999/airplay/releases/download/v0.5.0/AirPlay-Setup-0.5.0.exe");
        CHECK(a[1].name == "AirPlay-Setup-0.5.0.exe.sha256");
        CHECK(ui::widen(a[1].url) == ui::checksumUrlFor(ui::widen(a[0].url)));
    }

    CHECK(ui::parseReleaseAssets(R"({"tag_name":"v1","assets":[]})").empty());
    CHECK(ui::parseReleaseAssets(R"({"tag_name":"v1"})").empty());
    CHECK(ui::parseReleaseAssets("").empty());
    CHECK(ui::parseReleaseAssets("not json").empty());
    CHECK(ui::parseReleaseAssets(R"({"assets":[{"name":"a.exe","browser_download_url":)").empty());
    // "assets" only counts as a member of the release itself, not of a nested object
    CHECK(ui::parseReleaseAssets(
              R"({"author":{"assets":[{"name":"x.exe","browser_download_url":"https://github.com/x.exe"}]}})")
              .empty());
    // an asset missing either field is dropped, the rest kept
    const std::vector<ui::ReleaseAsset> b = ui::parseReleaseAssets(
        R"({"assets":[{"name":"a"},{"name":"b","browser_download_url":"https://github.com/b"}]})");
    CHECK(b.size() == 1 && b[0].name == "b");
}

void testFindString() {
    beginTest("findString");
    std::string v;
    CHECK(ui::findString(kRelease, 0, "tag_name", v) && v == "v0.5.0");
    CHECK(ui::findString(kRelease, 0, "html_url", v) &&
          v == "https://github.com/Lng999/airplay/releases/tag/v0.5.0");
    CHECK(ui::findString(kRelease, 0, "body", v) &&
          v == "see \"assets\": https://evil.com/y.exe\n- line \xC3\xA7");   // CRLF->LF, ç
    CHECK(!ui::findString(kRelease, 0, "missing", v));
    CHECK(!ui::findString(R"({"label":null,"x":"y"})", 0, "label", v));   // null is not a string
    CHECK(!ui::findString(R"({"size":5})", 0, "size", v));
    CHECK(!ui::findString(R"({"a":"unterminated)", 0, "a", v));
    CHECK(ui::findString(R"({ "a" :  "b" })", 0, "a", v) && v == "b");    // whitespace

    // `end` lets the caller walk repeated keys
    const std::string doc = R"([{"n":"one"},{"n":"two\\three"},{"n":"f\/our"}])";
    size_t next = 0;
    CHECK(ui::findString(doc, next, "n", v, &next) && v == "one");
    CHECK(ui::findString(doc, next, "n", v, &next) && v == "two\\three");
    CHECK(ui::findString(doc, next, "n", v, &next) && v == "f/our");
    CHECK(!ui::findString(doc, next, "n", v, &next));
}

void testCompareVersions() {
    beginTest("compareVersions");
    using ui::compareVersions;
    CHECK(compareVersions(L"0.4.0", L"0.4.0") == 0);
    CHECK(compareVersions(L"0.3", L"0.3.0") == 0);
    CHECK(compareVersions(L"0.4.0", L"0.4.1") < 0);
    CHECK(compareVersions(L"0.5.0", L"0.4.9") > 0);
    CHECK(compareVersions(L"0.10.0", L"0.9.0") > 0);     // numeric, not lexical
    CHECK(compareVersions(L"1", L"0.99.99") > 0);
    CHECK(compareVersions(L"0.4.0", L"0.4.0.1") < 0);
    CHECK(compareVersions(L"0.4.0-beta", L"0.4.0") == 0); // junk stops the scan
    CHECK(compareVersions(L"", L"0.0.0") == 0);
    CHECK(compareVersions(L"", L"0.0.1") < 0);
}

} // namespace

int main() {
    testAllowlistAccepts();
    testAllowlistScheme();
    testAllowlistHost();
    testAllowlistAuthorityTricks();
    testSha256Hex();
    testParseSha256File();
    testHexEquals();
    testParseReleaseAssets();
    testFindString();
    testCompareVersions();

    std::printf("%s: %d tests, %d checks, %d failed\n", g_failed ? "FAILED" : "OK", g_tests,
                g_checks, g_failed);
    return g_failed ? 1 : 0;
}
