#include "../cli.hpp"
#include "../util.hpp"
#include <gtest/gtest.h>

// ---------------------------------------------------------
// Tests
// ---------------------------------------------------------
TEST(LinePrefixingStreamBufTest, SimpleTransform) {
    std::stringbuf dest_buf;
    FilteringOutputStreamBuf prefix_buf(&dest_buf, LinePrefixingFilter{"> "});
    std::ostream os(&prefix_buf);

    os << "Line 0\n";
    os << "Line 1\n";
    os << "Line 2\n";
    os.flush();

    auto r = util::string_split(dest_buf.str(), '\n');
    EXPECT_EQ(r.size(), 3);
    EXPECT_EQ(r[0], "> Line 0");
    EXPECT_EQ(r[1], "> Line 1");
    EXPECT_EQ(r[2], "> Line 2");
}

// TEST(LinePrefixingStreamBufTest, PartialLineThenNewline) {
//     std::stringbuf dest_buf;
//
//     // Prepend ">> " at line start
//     auto transform_lambda = [at_line_start = true](std::streambuf& dst,
//                                                    int c) mutable {
//         if (at_line_start) {
//             dst.sputn(">> ", 3);
//             at_line_start = false;
//         }
//         dst.sputc(static_cast<char>(c));
//         if (c == '\n') {
//             at_line_start = true;
//         }
//     };
//
//     FilteringOutputStreamBuf prefix_buf(&dest_buf, transform_lambda);
//     std::ostream os(&prefix_buf);
//
//     // Write partial line, flush, then continue
//     os << "Hello";
//     os.flush();
//     // The prefix doesn't appear yet because we haven't seen a newline
//     // or a new line start. We just wrote characters.
//     // Now add the newline
//     os << '\n';
//     os.flush();
//
//     // We expect ">> Hello\n"
//     EXPECT_EQ(">> Hello\n", dest_buf.str());
// }
//
// // class ExampleTask : public Task {
// // public:
// //     ExampleTask(int count, bool force_fail = false)
// //         : count_(count)
// //         , force_fail_(force_fail)
// //         , stop_requested_(false)
// //     {}
// //
// //     bool run(std::ostream &out) override {
// //         for (int i = 0; i < count_; ++i) {
// //             if (stop_requested_) {
// //                 out << "[Task was stopped early]\n";
// //                 // Return false to indicate it didn’t complete fully
// //                 return false;
// //             }
// //             out << "Line " << i << "\n";
// //         }
// //         // If force_fail_ is true, pretend we failed at the end.
// //         return !force_fail_;
// //     }
// //
// //     void stop() override {
// //         stop_requested_ = true;
// //     }
// //
// // private:
// //     int count_;
// //     bool force_fail_;
// //     std::atomic_bool stop_requested_;
// // };
// //
// // // ---------------------------------------------------------
// // // TaskRunner tests
// // // ---------------------------------------------------------
// // TEST(TaskRunnerTest, AllTasksSucceed) {
// //     std::ostringstream output;
// //     TaskRunner runner(output);
// //
// //     runner.add_task("Alpha", std::make_shared<ExampleTask>(2)); // prints
// 2
// //     lines runner.add_task("Beta", std::make_shared<ExampleTask>(3));  //
// //     prints 3 lines
// //
// //     bool all_good = runner.run_all_tasks();
// //     EXPECT_TRUE(all_good);
// //
// //     std::string content = output.str();
// //     // [Alpha] lines
// //     EXPECT_NE(std::string::npos, content.find("[Alpha] Line 0"));
// //     EXPECT_NE(std::string::npos, content.find("[Alpha] Line 1"));
// //     // [Beta] lines
// //     EXPECT_NE(std::string::npos, content.find("[Beta] Line 0"));
// //     EXPECT_NE(std::string::npos, content.find("[Beta] Line 1"));
// //     EXPECT_NE(std::string::npos, content.find("[Beta] Line 2"));
// // }
// //
// // TEST(TaskRunnerTest, FailureStopsRunner) {
// //     std::ostringstream output;
// //     TaskRunner runner(output);
// //
// //     // Force fail at end of first task
// //     runner.add_task("First", std::make_shared<ExampleTask>(2,
// //     /*force_fail=*/true)); runner.add_task("Second",
// //     std::make_shared<ExampleTask>(2));
// //
// //     bool all_good = runner.run_all_tasks();
// //     EXPECT_FALSE(all_good);
// //
// //     std::string content = output.str();
// //     // The first task does run
// //     EXPECT_NE(std::string::npos, content.find("[First] Line 0"));
// //     EXPECT_NE(std::string::npos, content.find("[First] Line 1"));
// //     // The second task should NOT have run
// //     EXPECT_EQ(std::string::npos, content.find("[Second] Line 0"));
// // }
// //
// // TEST(TaskRunnerTest, StopAllTasksTreatsStopAsSuccess) {
// //     std::ostringstream output;
// //     TaskRunner runner(output);
// //
// //     auto t1 = std::make_shared<ExampleTask>(3);
// //     auto t2 = std::make_shared<ExampleTask>(3);
// //     runner.add_task("T1", t1);
// //     runner.add_task("T2", t2);
// //
// //     // We'll run the tasks in a separate thread and stop them partway.
// //     // Since there's no concurrency in run_all_tasks() by default,
// //     // we can simulate it by manually calling stop before finishing
// //     // the first task.
// //     //
// //     // But for the sake of demonstration, let's do a trick: we'll
// //     // modify ExampleTask to "simulate" a slow run. Or we'll just
// //     // stop before we call run_all_tasks to show that tasks are
// //     // considered success if they're never started.
// //     //
// //     // For a real concurrency scenario, you’d combine this approach
// //     // with multi-threading. Here, we simply call stop_all_tasks()
// //     // before run_all_tasks() to show how it's handled.
// //
// //     // Stopping before run => tasks are all considered "success."
// //     runner.stop_all_tasks();
// //     bool all_good = runner.run_all_tasks();
// //     EXPECT_TRUE(all_good);
// //
// //     // No tasks should actually have run
// //     std::string content = output.str();
// //     EXPECT_EQ(std::string::npos, content.find("[T1] Line 0"));
// //     EXPECT_EQ(std::string::npos, content.find("[T2] Line 0"));
// // }
