#include "util.h"
#include "util_io.h"
#include <gtest/gtest.h>

TEST(LinePrefixingStreamBufTest, SimpleTransform) {
    std::stringbuf dest_buf;
    FilteringOutputStreamBuf prefix_buf(&dest_buf, LinePrefixingFilter{"> "});
    std::ostream os(&prefix_buf);

    os << "Line 0\n";
    os << "Line 1\n";
    os << "Line 2\n";
    os.flush();

    auto r = string_split(dest_buf.str(), '\n');
    EXPECT_EQ(r.size(), 3);
    EXPECT_EQ(r[0], "> Line 0");
    EXPECT_EQ(r[1], "> Line 1");
    EXPECT_EQ(r[2], "> Line 2");
}
