#ifndef WIN32
#define BOOST_TEST_DYN_LINK
#endif

#ifdef STAND_ALONE
#define BOOST_TEST_MODULE Main
#endif

#include <sstream>
#include <locale.h>
#include <boost/test/unit_test.hpp>

#include "libed2k/constants.hpp"
#include "libed2k/file.hpp"
#include "libed2k/log.hpp"
#include "libed2k/transfer.hpp"
#include "libed2k/deadline_timer.hpp"
#include "libed2k/alert.hpp"
#include "libed2k/session_impl.hpp"
#include "common.hpp"

namespace libed2k {

#define TCOUNT 3

static session_settings ss;

class test_transfer_params_maker : public transfer_params_maker {
   public:
    static error_code m_errors[TCOUNT];
    static int m_total[TCOUNT];
    static int m_progress[TCOUNT];

    test_transfer_params_maker(alert_manager& am, const std::string& known_file);

   protected:
    void process_item();

   private:
    int m_index;
};

/**
  * test cancel file in progress
 */
class cancel_transfer_params_maker_progress : public transfer_params_maker {
   public:
    cancel_transfer_params_maker_progress(alert_manager& am, const std::string& known_file);

   protected:
    void process_item();
};

template <class Maker>
class session_impl_test : public aux::session_impl_base {
   public:
    session_impl_test(const session_settings& settings) : aux::session_impl_base(settings), m_tp_maker(m_alerts, "") {}
    virtual ~session_impl_test() {}

    transfer_handle add_transfer(add_transfer_params const&, error_code& ec) { return transfer_handle(); }
    boost::weak_ptr<transfer> find_transfer(const std::string& filename) { return boost::shared_ptr<transfer>(); }
    void remove_transfer(const transfer_handle& h, int options) {}
    std::vector<transfer_handle> get_transfers() { return std::vector<transfer_handle>(); }
    Maker m_tp_maker;  // do not name it m_tpm - original makers already named
};

error_code test_transfer_params_maker::m_errors[] = {errors::no_error, errors::filesize_is_zero,
                                                     errors::file_was_truncated};
int test_transfer_params_maker::m_total[] = {10, 20, 30};
int test_transfer_params_maker::m_progress[] = {10, 0, 23};

test_transfer_params_maker::test_transfer_params_maker(alert_manager& am, const std::string& known_file)
    : transfer_params_maker(am, known_file), m_index(0) {}

void test_transfer_params_maker::process_item() {
    DBG("process item " << m_index);
    add_transfer_params atp;
    atp.file_path = m_current_filepath;
    m_am.post_alert_should(transfer_params_alert(atp, m_errors[m_index]));
    ++m_index;
    m_index = m_index % TCOUNT;
}

cancel_transfer_params_maker_progress::cancel_transfer_params_maker_progress(alert_manager& am,
                                                                             const std::string& known_file)
    : transfer_params_maker(am, known_file) {}

void cancel_transfer_params_maker_progress::process_item() {
    try {
        while (1) {
            if (m_abort_current || m_abort) {
                throw libed2k_exception(errors::file_params_making_was_cancelled);
            }
        }
    } catch (libed2k_exception& e) {
        m_am.post_alert_should(transfer_params_alert(add_transfer_params(m_current_filepath), e.error()));
    }
}
}

#define WAIT_TPM(x)                                           \
    while (x.order_size() || !x.current_filepath().empty()) { \
    }

BOOST_AUTO_TEST_SUITE(test_share_files)

const char chRussianDirectory[] = {'\xEF', '\xBB', '\xBF', '\xD1', '\x80', '\xD1', '\x83', '\xD1', '\x81', '\xD1',
                                   '\x81', '\xD0', '\xBA', '\xD0', '\xB0', '\xD1', '\x8F', '\x20', '\xD0', '\xB4',
                                   '\xD0', '\xB8', '\xD1', '\x80', '\xD0', '\xB5', '\xD0', '\xBA', '\xD1', '\x82',
                                   '\xD0', '\xBE', '\xD1', '\x80', '\xD0', '\xB8', '\xD1', '\x8F', '\x00'};
const char chRussianFilename[] = {'\xD1', '\x80', '\xD1', '\x83', '\xD1', '\x81', '\xD1', '\x81',
                                  '\xD0', '\xBA', '\xD0', '\xB8', '\xD0', '\xB9', '\x20', '\xD1',
                                  '\x84', '\xD0', '\xB0', '\xD0', '\xB9', '\xD0', '\xBB', '\x00'};

BOOST_AUTO_TEST_CASE(test_string_conversions) {
    setlocale(LC_CTYPE, "");
    std::string strDirectory = chRussianDirectory;
    std::string strNative = libed2k::convert_to_native(libed2k::bom_filter(strDirectory));

    if (CHECK_BOM(strDirectory.size(), strDirectory)) {
        BOOST_CHECK_EQUAL(strDirectory.substr(3), libed2k::convert_from_native(strNative));
    }
}

BOOST_AUTO_TEST_CASE(test_concurrency) {
    const char* names[TCOUNT] = {"xxx", "yyy", "zzz"};
    libed2k::session_impl_test<libed2k::test_transfer_params_maker> sit(libed2k::ss);
    sit.m_alerts.set_alert_mask(libed2k::alert::all_categories);

    sit.m_tp_maker.start();
    sit.m_tp_maker.stop();
    sit.m_tp_maker.stop();
    sit.m_tp_maker.stop();
    sit.m_tp_maker.stop();
    sit.m_tp_maker.start();

    for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n) {
        sit.m_tp_maker.make_transfer_params(names[n]);
    }

    WAIT_TPM(sit.m_tp_maker)

    for (size_t n = 0; n < TCOUNT; ++n) {
        BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
        std::auto_ptr<libed2k::alert> a = sit.m_alerts.get();
        BOOST_REQUIRE(dynamic_cast<libed2k::transfer_params_alert*>(a.get()));
        BOOST_CHECK_EQUAL((dynamic_cast<libed2k::transfer_params_alert*>(a.get()))->m_ec,
                          libed2k::test_transfer_params_maker::m_errors[n]);
        BOOST_CHECK_EQUAL((dynamic_cast<libed2k::transfer_params_alert*>(a.get()))->m_atp.file_path,
                          std::string(names[n]));
    }
}

BOOST_AUTO_TEST_CASE(test_add_transfer_params_maker) {
    libed2k::session_impl_test<libed2k::transfer_params_maker> sit(libed2k::ss);
    sit.m_alerts.set_alert_mask(libed2k::alert::all_categories);

    test_files_holder tfh;
    const size_t sz = 5;
    const char* filename = "test_filename";

    std::pair<libed2k::size_type, libed2k::md4_hash> tmpl[sz] = {
        std::make_pair(100, libed2k::md4_hash::fromString("1AA8AFE3018B38D9B4D880D0683CCEB5")),
        std::make_pair(libed2k::PIECE_SIZE, libed2k::md4_hash::fromString("E76BADB8F958D7685B4549D874699EE9")),
        std::make_pair(libed2k::PIECE_SIZE + 1, libed2k::md4_hash::fromString("49EC2B5DEF507DEA73E106FEDB9697EE")),
        std::make_pair(libed2k::PIECE_SIZE * 4, libed2k::md4_hash::fromString("9385DCEF4CB89FD5A4334F5034C28893")),
        std::make_pair(libed2k::PIECE_SIZE + 4566, libed2k::md4_hash::fromString("9C7F988154D2C9AF16D92661756CF6B2"))};

    bool cancel = false;
    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        BOOST_REQUIRE(generate_test_file(tmpl[n].first, s.str()));
        tfh.hold(s.str());
        BOOST_CHECK_EQUAL(tmpl[n].second, libed2k::file2atp()(s.str(), cancel).first.file_hash);
    }

    sit.m_tpm.start();

    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        sit.m_tpm.make_transfer_params(s.str());
    }

    // wait hasher completed
    WAIT_TPM(sit.m_tpm);
    sit.m_tpm.stop();

    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
        std::auto_ptr<libed2k::alert> aptr = sit.m_alerts.get();
        libed2k::transfer_params_alert* a = dynamic_cast<libed2k::transfer_params_alert*>(aptr.get());
        BOOST_REQUIRE(a);
        BOOST_CHECK(!a->m_ec);
        BOOST_CHECK_MESSAGE(a->m_atp.file_hash == tmpl[n].second, s.str());
    }

    // start hashing and free resources
    sit.m_tpm.start();

    const char* zero_filename = "zero_filename.txt";
    tfh.hold(zero_filename);
    BOOST_REQUIRE(generate_test_file(0, zero_filename));
    sit.m_tpm.make_transfer_params(zero_filename);

    sit.m_tpm.make_transfer_params("non_exists");
    WAIT_TPM(sit.m_tpm)
    sit.m_tpm.stop();

    BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
    std::auto_ptr<libed2k::alert> aptr = sit.m_alerts.get();
    libed2k::transfer_params_alert* a = dynamic_cast<libed2k::transfer_params_alert*>(aptr.get());
    BOOST_REQUIRE(a);
    BOOST_CHECK_EQUAL(a->m_ec, libed2k::errors::make_error_code(libed2k::errors::filesize_is_zero));
    BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
    aptr = sit.m_alerts.get();
    a = dynamic_cast<libed2k::transfer_params_alert*>(aptr.get());
    BOOST_REQUIRE(a);
    BOOST_CHECK(a->m_ec);

    sit.m_tpm.start();

    // check abort processing
    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        sit.m_tpm.make_transfer_params(s.str());
    }

// wait for process begins
#ifdef WIN32
    Sleep(1000);
#else
    sleep(1);
#endif

    sit.m_tpm.stop();  // abort
    int iters = 0;

    while (sit.m_alerts.wait_for_alert(libed2k::milliseconds(10))) {
        ++iters;
        std::auto_ptr<libed2k::alert> a = sit.m_alerts.get();
        libed2k::transfer_params_alert* aptr = dynamic_cast<libed2k::transfer_params_alert*>(a.get());
        BOOST_REQUIRE(aptr);
        BOOST_CHECK(!aptr->m_ec || (aptr->m_ec == libed2k::errors::make_error_code(
                                                      libed2k::errors::file_params_making_was_cancelled)));
    }

    BOOST_CHECK_MESSAGE(iters, "Process nothing");

    // check abort processing
    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        sit.m_tpm.make_transfer_params(s.str());
    }

    // ok, cancel each file
    // check abort processing
    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        sit.m_tpm.cancel_transfer_params(s.str());
    }

    // we cancel all params - no signals
    BOOST_CHECK(!sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));

    sit.m_tpm.start();

    // check cancel each file
    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        sit.m_tpm.make_transfer_params(s.str());
    }

#ifdef WIN32
    Sleep(1000);
#else
    sleep(1);
#endif

    // ok, cancel each file
    // check abort processing
    for (size_t n = 0; n < sz; ++n) {
        std::stringstream s;
        s << filename << n;
        sit.m_tpm.cancel_transfer_params(s.str());
    }

    while (sit.m_alerts.wait_for_alert(libed2k::milliseconds(10))) {
        ++iters;
        std::auto_ptr<libed2k::alert> a = sit.m_alerts.get();
        libed2k::transfer_params_alert* aptr = dynamic_cast<libed2k::transfer_params_alert*>(a.get());
        BOOST_REQUIRE(aptr);
        BOOST_CHECK(!aptr->m_ec || (aptr->m_ec == libed2k::errors::make_error_code(
                                                      libed2k::errors::file_params_making_was_cancelled)));
    }

    DBG("test_add_transfer_params_maker {completed}");
}

BOOST_AUTO_TEST_CASE(test_cancel_filename_in_progress) {
    const char* filepath = "it is simple test name";
    libed2k::session_impl_test<libed2k::cancel_transfer_params_maker_progress> sit(libed2k::ss);
    sit.m_alerts.set_alert_mask(libed2k::alert::all_categories);
    sit.m_tp_maker.start();
    sit.m_tp_maker.make_transfer_params(filepath);
    while (sit.m_tp_maker.order_size()) {
    }  //!< wait file start processing
    sit.m_tp_maker.cancel_transfer_params(filepath);
    sit.m_tp_maker.make_transfer_params("some unknown file");
    while (sit.m_tp_maker.order_size()) {
    }  //!< wait file start processing
    sit.m_tp_maker.stop();

    // ok, we must have 3 alerts
    // 1. cancel params
    // 2. cancel params
    // 3. cancel hasher

    BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
    std::auto_ptr<libed2k::alert> a = sit.m_alerts.get();
    libed2k::transfer_params_alert* aptr = static_cast<libed2k::transfer_params_alert*>(a.get());
    BOOST_REQUIRE(aptr);
    BOOST_CHECK(aptr->m_ec == libed2k::errors::make_error_code(libed2k::errors::file_params_making_was_cancelled));

    BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
    a = sit.m_alerts.get();
    aptr = dynamic_cast<libed2k::transfer_params_alert*>(a.get());
    BOOST_REQUIRE(aptr);
    BOOST_CHECK(aptr->m_ec == libed2k::errors::make_error_code(libed2k::errors::file_params_making_was_cancelled));

    BOOST_REQUIRE(sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
    a = sit.m_alerts.get();
    aptr = dynamic_cast<libed2k::transfer_params_alert*>(a.get());
    BOOST_REQUIRE(aptr);
    BOOST_CHECK(aptr->m_ec == libed2k::errors::make_error_code(libed2k::errors::file_params_making_was_cancelled));

    BOOST_CHECK(!sit.m_alerts.wait_for_alert(libed2k::milliseconds(10)));
}

BOOST_AUTO_TEST_SUITE_END()
