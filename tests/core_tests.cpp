#include "beam/beam.hpp"

#include <iostream>
#include <string>
#include <string_view>

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, std::string_view label) {
        if (!condition) {
            std::cerr << "FAIL: " << label << '\n';
            ++failures;
        }
    };
    check(!beam::version().empty(), "version is available");
    for (const auto filename :
         {"hello.txt", "photo 2026.jpg", "archive.tar.gz", "README", "COM0.txt", "console.txt"}) {
        check(beam::is_safe_filename(filename), filename);
    }
    for (const auto filename : {"",
                                ".",
                                "..",
                                "../hello",
                                "../../.ssh/authorized_keys",
                                "/absolute",
                                "dir/file",
                                "dir\\file",
                                "C:hello",
                                ".hidden",
                                "trailing.",
                                "trailing ",
                                "bad?name",
                                "bad*name",
                                "bad|name",
                                "bad<name",
                                "bad>name",
                                "bad\"name",
                                "CON",
                                "con.txt",
                                "PrN",
                                "AUX.txt",
                                "nul",
                                "COM1.txt",
                                "com9",
                                "LPT1",
                                "lpt9.txt",
                                "CON .txt",
                                "CONIN$",
                                "conout$.txt",
                                " leading.txt"}) {
        check(!beam::is_safe_filename(filename), "reject unsafe name");
    }
    check(!beam::is_safe_filename(std::string_view{"ab\0cd", 5}), "reject embedded NUL");
    check(!beam::is_safe_filename("line\nfeed"), "reject control bytes");
    check(!beam::is_safe_filename("caf\xC3\xA9.txt"), "reject unsupported non-ASCII");
    check(beam::is_safe_filename(std::string(255, 'a')), "accept maximum length");
    check(!beam::is_safe_filename(std::string(256, 'a')), "reject excessive length");
    return failures == 0 ? 0 : 1;
}
