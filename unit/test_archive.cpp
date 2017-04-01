#ifndef WIN32
#define BOOST_TEST_DYN_LINK
#endif

#ifdef STAND_ALONE
#define BOOST_TEST_MODULE Main
#endif

#include <iostream>
#include <boost/test/unit_test.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/stream.hpp>
#include "libed2k/archive.hpp"
#include "libed2k/ctag.hpp"
#include "libed2k/packet_struct.hpp"
#include "libed2k/kademlia/kad_packet_struct.hpp"
#include "libed2k/log.hpp"
#include "libed2k/file.hpp"
#include "libed2k/base_connection.hpp"
#include "libed2k/util.hpp"

BOOST_AUTO_TEST_SUITE(test_archive)

typedef boost::iostreams::basic_array_source<char> ASourceDevice;
typedef boost::iostreams::basic_array_sink<char> ASinkDevice;

// symmetrical save/load
struct SerialStruct {
    boost::uint16_t m_nA;
    boost::uint16_t m_nB;
    SerialStruct(boost::uint16_t nA, boost::uint16_t nB) : m_nA(nA), m_nB(nB) {}

    template <typename Archive>
    void serialize(Archive& ar) {
        ar& m_nA;
        ar& m_nB;
    }

    bool operator==(const SerialStruct& ss) { return (m_nA == ss.m_nA && m_nB == ss.m_nB); }
};

// asymmetrical save/load
struct SplittedStruct {
    boost::uint16_t m_nA;
    boost::uint16_t m_nB;
    bool m_bWithC;
    boost::uint16_t m_nC;
    SplittedStruct(boost::uint16_t nA, boost::uint16_t nB, bool bWithC, boost::uint16_t nC)
        : m_nA(nA), m_nB(nB), m_bWithC(bWithC), m_nC(nC) {}

    template <typename Archive>
    void save(Archive& ar) const {
        ar& m_nA;
        ar& m_nB;
        ar& m_nC;
    }

    template <typename Archive>
    void load(Archive& ar) {
        ar& m_nA;
        ar& m_nB;

        if (m_bWithC) {
            ar& m_nC;
        }
    }

    LIBED2K_SERIALIZATION_SPLIT_MEMBER()
};

// asymmetrical private members
class HideSplitted {
   public:
    friend class libed2k::archive::access;

    HideSplitted(boost::uint16_t nA, char cB, char cC) : m_nA(nA), m_cB(cB), m_cC(cC) {}

    template <typename Archive>
    void save(Archive& ar) const {
        ar& m_nA;
    }

    template <typename Archive>
    void load(Archive& ar) {
        ar& m_nA;
        ar& m_cB;
        ar& m_cC;
    }

    LIBED2K_SERIALIZATION_SPLIT_MEMBER()

    boost::uint16_t getA() const { return (m_nA); }

    char getB() const { return (m_cB); }

    char getC() const { return (m_cC); }

    bool operator==(const HideSplitted& hs) const { return (m_nA == hs.m_nA && m_cB == hs.m_cB && m_cC == hs.m_cC); }

   private:
    boost::uint16_t m_nA;
    char m_cB;
    char m_cC;
};

BOOST_AUTO_TEST_CASE(test_memory_archive) {
    APP("test_memory_archive");
    // it is test source array
    const boost::uint16_t m_source_archive[10] = {0x0102, 0x0304, 0x0506, 0x0708, 0x090A,
                                                  0x0B0C, 0x0D0E, 0x3040, 0x3020, 0xFFDD};
    const char* dataPtr = (const char*)&m_source_archive[0];
    SerialStruct ss_struct1(1, 2);

    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer(dataPtr, sizeof(m_source_archive));
    std::istream in_array_stream(&array_source_buffer);
    libed2k::archive::ed2k_iarchive in_array_archive(in_array_stream);
    in_array_archive >> ss_struct1;

    BOOST_CHECK_EQUAL(ss_struct1.m_nA, m_source_archive[0]);
    BOOST_CHECK_EQUAL(ss_struct1.m_nB, m_source_archive[1]);

    SplittedStruct sp_struct1(1, 2, true, 100);
    in_array_archive >> sp_struct1;

    BOOST_CHECK_EQUAL(sp_struct1.m_nA, m_source_archive[2]);
    BOOST_CHECK_EQUAL(sp_struct1.m_nB, m_source_archive[3]);
    BOOST_CHECK_EQUAL(sp_struct1.m_nC, m_source_archive[4]);

    SplittedStruct sp_struct2(1, 2, false, 100);
    in_array_archive >> sp_struct2;

    BOOST_CHECK_EQUAL(sp_struct2.m_nA, m_source_archive[5]);
    BOOST_CHECK_EQUAL(sp_struct2.m_nB, m_source_archive[6]);
    BOOST_CHECK_EQUAL(sp_struct2.m_nC, 100);

    HideSplitted hs1(1, 'A', 'B');
    in_array_archive >> hs1;

    BOOST_CHECK_EQUAL(hs1.getA(), m_source_archive[7]);
    BOOST_CHECK_EQUAL(hs1.getB(), ' ');
    BOOST_CHECK_EQUAL(hs1.getC(), '0');

    boost::uint32_t nLongData;
    BOOST_CHECK_THROW((in_array_archive >> nLongData), libed2k::libed2k_exception);

    // test arrays
    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer2(dataPtr, sizeof(m_source_archive));
    std::istream in_array_stream2(&array_source_buffer2);
    libed2k::archive::ed2k_iarchive in_array_archive2(in_array_stream2);

    boost::uint16_t vData[4] = {0, 1, 2, 3};

    in_array_archive2.raw_read((char*)vData, sizeof(vData));

    BOOST_CHECK_EQUAL(m_source_archive[0], vData[0]);
    BOOST_CHECK_EQUAL(m_source_archive[1], vData[1]);
    BOOST_CHECK_EQUAL(m_source_archive[2], vData[2]);
    BOOST_CHECK_EQUAL(m_source_archive[3], vData[3]);
    in_array_stream2.seekg(4, std::ios_base::cur);
    boost::uint16_t nData6;
    in_array_archive2 >> nData6;
    BOOST_CHECK_EQUAL(m_source_archive[6], nData6);

    // simple write operations
    std::ostringstream sstream(std::ios_base::binary);
    libed2k::archive::ed2k_oarchive out_string_archive(sstream);
    out_string_archive << ss_struct1;
    BOOST_CHECK_EQUAL(sstream.str().length(), sizeof(SerialStruct));
    out_string_archive << hs1;
    BOOST_CHECK_EQUAL(sstream.str().length(), sizeof(SerialStruct) + sizeof(boost::uint16_t));
    bool bData = false;
    out_string_archive << bData;
    std::string strData = "Simple";
    boost::uint16_t nStrDataSize = static_cast<boost::uint16_t>(strData.size());
    out_string_archive << nStrDataSize;
    out_string_archive << strData;
    BOOST_CHECK_EQUAL(sstream.str().length(), sizeof(SerialStruct) + sizeof(boost::uint16_t) + sizeof(bool) +
                                                  sizeof(boost::uint16_t) + strData.size());
}

BOOST_AUTO_TEST_CASE(test_container_holder) {
    // correct string container contains string "01"
    const boost::uint8_t m_source_archive[] = {'\x02', '\x00', '\x00', '\x00', '\x30', '\x31'};
    libed2k::container_holder<boost::uint32_t, std::string> string1;
    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer((const char*)m_source_archive,
                                                                       sizeof(m_source_archive));
    std::istream in_array_stream(&array_source_buffer);
    libed2k::archive::ed2k_iarchive in_array_archive(in_array_stream);
    in_array_archive >> string1;
    BOOST_CHECK_EQUAL(string1.m_collection, "01");

    // incorrect string container with enormous string
    const boost::uint8_t m_source_archive_inc[] = {'\x02', '\x00', '\x00', '\x0F', '\x30', '\x31',
                                                   '\x11', '\x11', '\x11', '\x11', '\x11'};
    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer_inc((const char*)m_source_archive_inc,
                                                                           sizeof(m_source_archive_inc));
    std::istream in_array_stream_inc(&array_source_buffer_inc);
    libed2k::archive::ed2k_iarchive in_array_archive_inc(in_array_stream_inc);
    BOOST_CHECK_THROW(in_array_archive_inc >> string1, libed2k::libed2k_exception);
}

BOOST_AUTO_TEST_CASE(test_file_archive) {
    SerialStruct ss1(1, 2);
    SplittedStruct sp1(1, 2, true, 2);
    HideSplitted hs1(1, 'B', 'C');
    std::string strData1 = "File test data";
    boost::uint8_t nDataLength1 = static_cast<boost::uint8_t>(strData1.size());

    {
        char B = 'B';
        char C = 'C';
        std::ofstream ofs("./test.bin", std::ios_base::binary);
        libed2k::archive::ed2k_oarchive ofa(ofs);
        ofa << ss1;
        ofa << sp1;
        ofa << hs1;
        ofa << B;  // doesn't save by save call
        ofa << C;  // doesn't save by save call
        ofa << nDataLength1;
        ofa << strData1;
        ofs.flush();
    }

    SerialStruct ss2(100, 902);
    SplittedStruct sp2(1009, 299, true, 1212);
    HideSplitted hs2(341, 'X', 'Y');
    std::string strData2 = "";
    boost::uint8_t nDataLength2 = 0;

    {
        std::ifstream ifs("./test.bin", std::ios_base::binary);
        libed2k::archive::ed2k_iarchive ifa(ifs);
        ifa >> ss2;
        ifa >> sp2;
        ifa >> hs2;
        ifa >> nDataLength2;
        strData2.resize(static_cast<size_t>(nDataLength2));
        ifa >> strData2;
    }

    BOOST_CHECK(ss2 == ss1);
    BOOST_CHECK(hs2 == hs1);
    BOOST_CHECK_EQUAL(sp2.m_nA, sp1.m_nA);
    BOOST_CHECK_EQUAL(sp2.m_nB, sp1.m_nB);
    BOOST_CHECK_EQUAL(sp2.m_nC, sp1.m_nC);
    BOOST_CHECK(strData2 == strData1);
}

BOOST_AUTO_TEST_CASE(test_tag_list) {
    libed2k::tag_list<boost::uint16_t> tl;
    boost::shared_ptr<libed2k::base_tag> ptag(new libed2k::typed_tag<float>(10.6f, "XXX", true));
    float fTemplate = 1292.54f;
    const char* pData = (const char*)&fTemplate;
    libed2k::md4_hash md4 = libed2k::md4_hash::fromString("000102030405060708090A0B0C0D0E0F");

    std::vector<char> vBlob;
    vBlob.push_back('\x0D');
    vBlob.push_back('\x0A');
    vBlob.push_back('\x0B');

    const boost::uint8_t m_source_archive[] = {
        /* 2 bytes list size*/ '\x09',
        '\x00',
        /*1 byte*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_UINT8 | 0x80),
        '\x10',
        '\xED',
        /*2 bytes*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_UINT16 | 0x80),
        '\x11',
        '\x0A',
        '\x0D',
        /*8 bytes*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_UINT64),
        '\x04',
        '\x00',
        '\x30',
        '\x31',
        '\x32',
        '\x33',
        '\x01',
        '\x02',
        '\x03',
        '\x04',
        '\x05',
        '\x06',
        '\x07',
        '\x08',
        /*variable string*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_STRING),
        '\x04',
        '\x00',
        'A',
        'B',
        'C',
        'D',
        '\x06',
        '\x00',
        'S',
        'T',
        'R',
        'I',
        'N',
        'G',
        /*defined string*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_STR5),
        '\x04',
        '\x00',
        'I',
        'V',
        'A',
        'N',
        'A',
        'P',
        'P',
        'L',
        'E',
        /*blob*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_BLOB | 0x80),
        '\x0A',
        '\x03',
        '\x00',
        '\x00',
        '\x00',
        '\x0D',
        '\x0A',
        '\x0B',
        /*float*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_FLOAT32 | 0x80),
        '\x15',
        static_cast<boost::uint8_t>(*pData),
        static_cast<boost::uint8_t>(*(pData + 1)),
        static_cast<boost::uint8_t>(*(pData + 2)),
        static_cast<boost::uint8_t>(*(pData + 3)),
        /*bool*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_BOOL | 0x80),
        '\x15',
        '\x01',
        /*hash*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_HASH16 | 0x80),
        '\x20',
        '\x00',
        '\x01',
        '\x02',
        '\x03',
        '\x04',
        '\x05',
        '\x06',
        '\x07',
        '\x08',
        '\x09',
        '\x0A',
        '\x0B',
        '\x0C',
        '\x0D',
        '\x0E',
        '\x0F',
        /*invalid blob*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_BLOB | 0x80),
        '\x0A',
        '\xFF',
        '\xFF',
        '\xEE',
        '\xFF',
        '\x0D',
        '\x0A',
        '\x0B'};

    const char* dataPtr = (const char*)&m_source_archive[0];
    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer(dataPtr, sizeof(m_source_archive));
    std::istream in_array_stream(&array_source_buffer);
    libed2k::archive::ed2k_iarchive in_array_archive(in_array_stream);
    in_array_archive >> tl;

    BOOST_CHECK_EQUAL(tl.size(), static_cast<unsigned short>(m_source_archive[0]));
    BOOST_CHECK(tl[0]->getType() == libed2k::TAGTYPE_UINT8);
    BOOST_CHECK(tl[1]->getType() == libed2k::TAGTYPE_UINT16);
    BOOST_CHECK(tl[2]->getType() == libed2k::TAGTYPE_UINT64);
    BOOST_CHECK(tl[3]->getType() == libed2k::TAGTYPE_STRING);
    BOOST_CHECK(tl[4]->getType() == libed2k::TAGTYPE_STR5);
    BOOST_CHECK(tl[5]->getType() == libed2k::TAGTYPE_BLOB);
    BOOST_CHECK(tl[6]->getType() == libed2k::TAGTYPE_FLOAT32);
    BOOST_CHECK(tl[7]->getType() == libed2k::TAGTYPE_BOOL);

    libed2k::tag_list<boost::uint16_t> tl2;
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(new libed2k::typed_tag<boost::uint8_t>('\xED', 0x10, true)));
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(new libed2k::typed_tag<boost::uint16_t>(0x0D0A, '\x11', true)));
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(
        new libed2k::typed_tag<boost::uint64_t>(0x0807060504030201LL, "0123", true)));
    tl2.add_tag(
        boost::shared_ptr<libed2k::base_tag>(new libed2k::string_tag("STRING", libed2k::TAGTYPE_STRING, "ABCD", true)));
    tl2.add_tag(
        boost::shared_ptr<libed2k::base_tag>(new libed2k::string_tag("APPLE", libed2k::TAGTYPE_STR5, "IVAN", true)));
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(new libed2k::array_tag(vBlob, '\x0A', true)));
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(new libed2k::typed_tag<float>(fTemplate, '\x15', true)));
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(new libed2k::typed_tag<bool>(true, '\x15', true)));
    tl2.add_tag(boost::shared_ptr<libed2k::base_tag>(new libed2k::typed_tag<libed2k::md4_hash>(md4, '\x20', true)));

    BOOST_CHECK(tl == tl2);

    // ok, prepare for save-restore
    tl.clear();
    std::stringstream sstream_out(std::ios::out | std::ios::in | std::ios::binary);
    libed2k::archive::ed2k_oarchive out_string_archive(sstream_out);
    out_string_archive << tl2;

    sstream_out.flush();
    sstream_out.seekg(0, std::ios::beg);

    BOOST_REQUIRE(sstream_out.good());

    libed2k::archive::ed2k_iarchive in_string_archive(sstream_out);
    in_string_archive >> tl;
    BOOST_CHECK(tl == tl2);
}

BOOST_AUTO_TEST_CASE(test_tag_errors) {
    libed2k::tag_list<boost::uint16_t> tl;
    const boost::uint8_t m_source_archive[] = {
        '\x02',
        '\x00',
        /*hash*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_HASH16 | 0x80),
        '\x20',
        '\x00',
        '\x01',
        '\x02',
        '\x03',
        '\x04',
        '\x05',
        '\x06',
        '\x07',
        '\x08',
        '\x09',
        '\x0A',
        '\x0B',
        '\x0C',
        '\x0D',
        '\x0E',
        '\x0F',
        /*invalid blob*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_BLOB | 0x80),
        '\x0A',
        '\xFF',
        '\xFF',
        '\xEE',
        '\xFF',
        '\x0D',
        '\x0A',
        '\x0B',
    };

    const char* dataPtr = (const char*)&m_source_archive[0];
    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer(dataPtr, sizeof(m_source_archive));
    std::istream in_array_stream(&array_source_buffer);

    libed2k::archive::ed2k_iarchive in_array_archive(in_array_stream);
    BOOST_CHECK_THROW(in_array_archive >> tl, libed2k::libed2k_exception);
}

BOOST_AUTO_TEST_CASE(test_tag_conversation) {
    libed2k::string_tag s1("TEST", '\x10', true);                              //!< auto convert
    libed2k::string_tag s2("TEST DATA", "name", true);                         //!< auto convert
    libed2k::string_tag s3("TEST", libed2k::TAGTYPE_STRING, "my name", true);  //!< none
    libed2k::string_tag s4("PLAN", libed2k::FT_CATEGORY, false);               //!< no auto convert

    std::string strRes = s1;
    BOOST_CHECK_EQUAL(s1.getNameId(), '\x10');
    BOOST_CHECK(std::string("TEST") == strRes);
    BOOST_CHECK_EQUAL(s1.getType(), libed2k::TAGTYPE_STR4);  // auto conversation

    strRes = s2;
    BOOST_CHECK_EQUAL(s2.getNameId(), 0);
    BOOST_CHECK(std::string("TEST DATA") == strRes);
    BOOST_CHECK_EQUAL(s2.getType(), libed2k::TAGTYPE_STR9);  // auto conversation

    strRes = s3;
    BOOST_CHECK_EQUAL(s3.getNameId(), 0);
    BOOST_CHECK_EQUAL(s3.getName(), "my name");
    BOOST_CHECK(std::string("TEST") == strRes);
    BOOST_CHECK_EQUAL(s3.getType(), libed2k::TAGTYPE_STRING);  // no auto conversion

    strRes = s4;
    BOOST_CHECK_EQUAL(s4.getNameId(), libed2k::FT_CATEGORY);
    BOOST_CHECK(s4.getName().empty());
    BOOST_CHECK(std::string("PLAN") == strRes);
    BOOST_CHECK_EQUAL(s3.getType(), libed2k::TAGTYPE_STRING);  // no auto conversion

    boost::uint16_t n1 = 1000;
    boost::uint16_t n1_d;
    boost::shared_ptr<libed2k::base_tag> pt = libed2k::make_typed_tag(n1, std::string("some name"), true);
    BOOST_REQUIRE(pt->getType() == libed2k::TAGTYPE_UINT16);
    n1_d = *((libed2k::typed_tag<boost::uint16_t>*)(pt.get()));
    BOOST_CHECK(n1 == n1_d);

    const boost::uint8_t m_source_archive[] = {
        /* 2 bytes list size*/ '\x03',
        '\x00',
        /*1 byte*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_UINT8),
        '\x01',
        '\x00',
        '\xED',
        '\xFA',
        /*bool array*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_BOOLARRAY | 0x80),
        '\x11',
        '\x08',
        '\x00',
        '\xFF',
        '\x0F',
        /*8 bytes*/ static_cast<boost::uint8_t>(libed2k::TAGTYPE_UINT64),
        '\x04',
        '\x00',
        '\x30',
        '\x31',
        '\x32',
        '\x33',
        '\x01',
        '\x02',
        '\x03',
        '\x04',
        '\x05',
        '\x06',
        '\x07',
        '\x08'};

    libed2k::tag_list<boost::uint16_t> tl;
    const char* dataPtr = (const char*)&m_source_archive[0];
    boost::iostreams::stream_buffer<ASourceDevice> array_source_buffer(dataPtr, sizeof(m_source_archive));
    std::istream in_array_stream(&array_source_buffer);
    libed2k::archive::ed2k_iarchive in_array_archive(in_array_stream);
    in_array_archive >> tl;

    BOOST_REQUIRE_EQUAL(tl.size(), 2U);
    BOOST_CHECK_EQUAL(tl[0]->getNameId(), 0xED);
    BOOST_CHECK_EQUAL(tl[1]->getType(), libed2k::TAGTYPE_UINT64);
}

BOOST_AUTO_TEST_CASE(test_tags_mixed) {
    boost::uint16_t n1 = 1000;
    boost::uint64_t n2 = 32323267673UL;
    std::vector<char> vData(1000);
    libed2k::tag_list<boost::uint16_t> src_list;
    src_list.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::FT_FILENAME, true));
    src_list.add_tag(libed2k::make_string_tag(std::string("IVANANDPLAN"), libed2k::FT_FILENAME, false));
    src_list.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::FT_FILENAME, false));
    src_list.add_tag(libed2k::make_blob_tag(vData, libed2k::FT_AICH_HASH, false));
    src_list.add_tag(libed2k::make_typed_tag(n1, "I'm integer", false));
    src_list.add_tag(libed2k::make_typed_tag(n2, "I'm integer", true));

    std::stringstream sstream_out(std::ios::out | std::ios::in | std::ios::binary);
    libed2k::archive::ed2k_oarchive out_string_archive(sstream_out);
    out_string_archive << src_list;

    // go to begin
    sstream_out.seekg(0, std::ios::beg);

    libed2k::tag_list<boost::uint16_t> dst_list;
    libed2k::archive::ed2k_iarchive in_string_archive(sstream_out);
    in_string_archive >> dst_list;

    BOOST_CHECK(src_list == dst_list);
}

BOOST_AUTO_TEST_CASE(test_tags_getters) {
    boost::uint16_t n16 = 1000;
    boost::uint64_t n64 = 32323267673UL;
    boost::uint32_t n32 = 233332;
    boost::uint8_t n8 = '\x10';
    bool bTag = true;
    std::vector<char> vBlob;
    float fTag = 1129.4;
    libed2k::tag_list<boost::uint16_t> src_list;

    bool stringTest[] = {true, true, false, false, false, false, false, false, false, false};
    bool intTest[] = {false, false, true, true, true, true, false, false, false, false};
    bool floatTest[] = {false, false, false, false, false, false, false, false, true, false};
    bool boolTest[] = {false, false, false, false, false, false, true, false, false, false};
    bool blobTest[] = {false, false, false, false, false, false, false, true, false, false};
    bool hashTest[] = {false, false, false, false, false, false, false, false, false, true};

    src_list.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::CT_NAME, true));                // 0
    src_list.add_tag(libed2k::make_string_tag(std::string("IVANANDPLAN"), libed2k::FT_FILEFORMAT, false));  // 1
    src_list.add_tag(libed2k::make_typed_tag(n8, libed2k::CT_SERVER_FLAGS, false));                         // 2
    src_list.add_tag(libed2k::make_typed_tag(n16, libed2k::FT_FILESIZE, true));                             // 3
    src_list.add_tag(libed2k::make_typed_tag(n32, libed2k::CT_EMULE_RESERVED13, false));                    // 4
    src_list.add_tag(libed2k::make_typed_tag(n64, libed2k::FT_ATREQUESTED, true));                          // 5
    src_list.add_tag(libed2k::make_typed_tag(bTag, libed2k::FT_FLAGS, true));                               // 6
    src_list.add_tag(libed2k::make_blob_tag(vBlob, libed2k::FT_DL_PREVIEW, true));                          // 7
    src_list.add_tag(libed2k::make_typed_tag(fTag, libed2k::FT_MEDIA_ALBUM, true));                         // 8
    src_list.add_tag(
        libed2k::make_typed_tag(libed2k::md4_hash(libed2k::md4_hash::terminal()), libed2k::FT_AICH_HASH, true));  // 9

    BOOST_REQUIRE_EQUAL(src_list.size(), 10U);

    for (size_t n = 0; n < src_list.size(); n++) {
        if (stringTest[n]) {
            BOOST_CHECK_NO_THROW(src_list[n]->asString());
        } else {
            BOOST_CHECK_THROW(src_list[n]->asString(), libed2k::libed2k_exception);
        }

        if (intTest[n]) {
            BOOST_CHECK_NO_THROW(src_list[n]->asInt());
        } else {
            BOOST_CHECK_THROW(src_list[n]->asInt(), libed2k::libed2k_exception);
        }

        if (floatTest[n]) {
            BOOST_CHECK_NO_THROW(src_list[n]->asFloat());
        } else {
            BOOST_CHECK_THROW(src_list[n]->asFloat(), libed2k::libed2k_exception);
        }

        if (boolTest[n]) {
            BOOST_CHECK_NO_THROW(src_list[n]->asBool());
        } else {
            BOOST_CHECK_THROW(src_list[n]->asBool(), libed2k::libed2k_exception);
        }

        if (blobTest[n]) {
            BOOST_CHECK_NO_THROW(src_list[n]->asBlob());
        } else {
            BOOST_CHECK_THROW(src_list[n]->asBlob(), libed2k::libed2k_exception);
        }

        if (hashTest[n]) {
            BOOST_CHECK_NO_THROW(src_list[n]->asHash());
        } else {
            BOOST_CHECK_THROW(src_list[n]->asHash(), libed2k::libed2k_exception);
        }
    }

    size_t nCount = 0;
    std::string strName;
    std::string strFilename;
    float fValue;
    boost::uint64_t n_8 = 0;
    boost::uint64_t n_16 = 0;
    boost::uint64_t n_32 = 0;
    boost::uint64_t n_64 = 0;
    bool bDst = false;
    libed2k::md4_hash hRes;

    for (size_t n = 0; n < src_list.size(); n++) {
        boost::shared_ptr<libed2k::base_tag> ptag = src_list[n];
        switch (ptag->getNameId()) {
            case libed2k::CT_NAME:
                strName = ptag->asString();
                nCount++;
                break;
            case libed2k::FT_FILEFORMAT:
                strFilename = ptag->asString();
                nCount++;
                break;
            case libed2k::FT_AICH_HASH:
                hRes = ptag->asHash();
                nCount++;
                break;
            case libed2k::FT_MEDIA_ALBUM:
                fValue = ptag->asFloat();
                nCount++;
                break;
            case libed2k::CT_SERVER_FLAGS:
                n_8 = ptag->asInt();
                nCount++;
                break;

            case libed2k::FT_FILESIZE:
                n_16 = ptag->asInt();
                nCount++;
                break;
            case libed2k::CT_EMULE_RESERVED13:
                n_32 = ptag->asInt();
                nCount++;
                break;
            case libed2k::FT_ATREQUESTED:
                n_64 = ptag->asInt();
                nCount++;
                break;
            case libed2k::FT_FLAGS:
                bDst = ptag->asBool();
                nCount++;
                break;
            case libed2k::FT_DL_PREVIEW:
                nCount++;
                break;
            default:
                break;
        }
    }

    BOOST_REQUIRE_EQUAL(nCount, src_list.size());
    BOOST_CHECK_EQUAL(strName, std::string("IVAN"));
    BOOST_CHECK_EQUAL(strFilename, std::string("IVANANDPLAN"));
    BOOST_CHECK_EQUAL(fValue, fTag);
    BOOST_CHECK_EQUAL(n_8, n8);
    BOOST_CHECK_EQUAL(n_16, n16);
    BOOST_CHECK_EQUAL(n_32, n32);
    BOOST_CHECK_EQUAL(n_64, n64);
    BOOST_CHECK_EQUAL(bDst, bTag);
    BOOST_CHECK(hRes == libed2k::md4_hash(libed2k::md4_hash::terminal()));
}

BOOST_AUTO_TEST_CASE(test_list_getters) {
    boost::uint32_t n32 = 23;
    libed2k::tag_list<boost::uint16_t> src_list;
    src_list.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::CT_NAME, true));
    src_list.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ATACCEPTED, true));
    src_list.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ED2K_MEDIA_LENGTH, false));
    src_list.add_tag(libed2k::make_string_tag("Charoff", libed2k::FT_ED2K_MEDIA_ARTIST, false));

    BOOST_CHECK_EQUAL(src_list.getStringTagByNameId(libed2k::CT_NAME), std::string("IVAN"));
    BOOST_CHECK_EQUAL(src_list.getStringTagByNameId(libed2k::FT_ATACCEPTED), std::string(""));  // incorrect type
    BOOST_CHECK_EQUAL(src_list.getStringTagByNameId(libed2k::FT_FILESIZE), std::string(""));    // tag not exists
    BOOST_CHECK(src_list.getTagByName(libed2k::FT_ED2K_MEDIA_LENGTH));  // tag with name FT_ED2K_MEDIA_LENGTH exists
    BOOST_CHECK_EQUAL(src_list.getTagByName(libed2k::FT_ED2K_MEDIA_LENGTH)->asInt(),
                      n32);  // tag with name FT_ED2K_MEDIA_LENGTH exists
    BOOST_CHECK(
        !src_list.getTagByName(libed2k::FT_ED2K_MEDIA_BITRATE));  // tag with name FT_ED2K_MEDIA_BITRATE not exists

    if (boost::shared_ptr<libed2k::base_tag> p = src_list.getTagByName(libed2k::FT_ED2K_MEDIA_ARTIST)) {
        BOOST_CHECK_EQUAL(p->asString(), "Charoff");
    } else {
        BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_CASE(test_tag_list_equals) {
    boost::uint32_t n32 = 33;
    boost::uint32_t n23 = 23;

    libed2k::tag_list<boost::uint16_t> list1;
    libed2k::tag_list<boost::uint16_t> list2;
    libed2k::tag_list<boost::uint16_t> list3;
    libed2k::tag_list<boost::uint16_t> list4;
    libed2k::tag_list<boost::uint16_t> list5;

    list1.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::CT_NAME, false));
    list1.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ATACCEPTED, false));
    list1.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ED2K_MEDIA_LENGTH, false));

    list2.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ATACCEPTED, false));
    list2.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ED2K_MEDIA_LENGTH, true));
    list2.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::CT_NAME, true));

    list3.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::CT_NAME, false));

    list5.add_tag(libed2k::make_string_tag(std::string("IVAN"), libed2k::CT_NAME, false));
    list5.add_tag(libed2k::make_typed_tag(n23, libed2k::FT_ATACCEPTED, false));
    list5.add_tag(libed2k::make_typed_tag(n32, libed2k::FT_ED2K_MEDIA_LENGTH, false));

    BOOST_CHECK(list1 == list2);
    BOOST_CHECK(!(list1 == list3));
    BOOST_CHECK(!(list2 == list4));
    BOOST_CHECK(!(list1 == list5));
}

BOOST_AUTO_TEST_CASE(test_packets) {
    libed2k::shared_file_entry sh(libed2k::md4_hash::terminal(), 100, 12);
    libed2k::shared_files_list flist;
    flist.m_collection.push_back(libed2k::shared_file_entry(libed2k::md4_hash::terminal(), 1, 2));
    flist.m_collection.push_back(libed2k::shared_file_entry(libed2k::md4_hash::terminal(), 3, 4));
    flist.m_collection.push_back(libed2k::shared_file_entry(libed2k::md4_hash::terminal(), 4, 5));

    std::stringstream sstream_out(std::ios::out | std::ios::in | std::ios::binary);
    libed2k::archive::ed2k_oarchive out_string_archive(sstream_out);
    out_string_archive << sh;
    out_string_archive << flist;

    sstream_out.seekg(0, std::ios::beg);
    libed2k::archive::ed2k_iarchive in_string_archive(sstream_out);
    libed2k::shared_file_entry dsh;
    libed2k::shared_files_list flist2;
    in_string_archive >> dsh;
    in_string_archive >> flist2;

    BOOST_CHECK(sh.m_hFile == dsh.m_hFile);
    BOOST_REQUIRE(flist.m_size == flist2.m_size);

    BOOST_CHECK(flist.m_collection[0].m_network_point.m_nIP == 1);
    BOOST_CHECK(flist.m_collection[1].m_network_point.m_nIP == 3);
    BOOST_CHECK(flist.m_collection[2].m_network_point.m_nIP == 4);
    BOOST_CHECK(flist.m_collection[0].m_network_point.m_nPort == 2);
    BOOST_CHECK(flist.m_collection[1].m_network_point.m_nPort == 4);
    BOOST_CHECK(flist.m_collection[2].m_network_point.m_nPort == 5);
}

BOOST_AUTO_TEST_CASE(test_emule_collection) {
#ifdef WIN32
    libed2k::emule_collection ec = libed2k::emule_collection::fromFile("../../unit/test_collection.emulecollection");
#else
    libed2k::emule_collection ec = libed2k::emule_collection::fromFile("test_collection.emulecollection");
#endif

    BOOST_REQUIRE_EQUAL(ec.m_files.size(), 3U);
    BOOST_CHECK_EQUAL(ec.m_files.at(0).m_filename, "file3.txt");
    BOOST_CHECK_EQUAL(ec.m_files.at(1).m_filename, "file2.txt");
    BOOST_CHECK_EQUAL(ec.m_files.at(2).m_filename, "file1.txt");
#ifdef WIN32
    libed2k::emule_collection ec_text =
        libed2k::emule_collection::fromFile("../../unit/test_text_collection.emulecollection");
#else
    libed2k::emule_collection ec_text = libed2k::emule_collection::fromFile("test_text_collection.emulecollection");
#endif

    BOOST_REQUIRE_EQUAL(ec_text.m_files.size(), 3U);
    BOOST_CHECK_EQUAL(ec_text.m_files.at(0).m_filename, "1.txt");
    BOOST_CHECK_EQUAL(ec_text.m_files.at(1).m_filename, "2.txt");
    BOOST_CHECK_EQUAL(ec_text.m_files.at(2).m_filename, "xxx.txt");

    BOOST_CHECK_EQUAL(ec_text.m_files.at(0).m_filehash,
                      libed2k::md4_hash::fromString("DB48A1C00CC972488C29D3FEC9F15A79"));
    BOOST_CHECK_EQUAL(ec_text.m_files.at(1).m_filehash,
                      libed2k::md4_hash::fromString("DB48A1C00CC972488C29D3FEC9F16A79"));
    BOOST_CHECK_EQUAL(ec_text.m_files.at(2).m_filehash,
                      libed2k::md4_hash::fromString("DB48A1C00CC972488C29D3FEC9F15A79"));

    BOOST_CHECK(ec.save("./txt_test.emulecollection", false));
    BOOST_CHECK(ec.save("./binary_test.emulecollection", true));

    libed2k::emule_collection incoming_ec = libed2k::emule_collection::fromFile("./txt_test.emulecollection");
    BOOST_CHECK(ec == incoming_ec);
    incoming_ec = libed2k::emule_collection::fromFile("./binary_test.emulecollection");
    BOOST_CHECK(ec == incoming_ec);
}

BOOST_AUTO_TEST_CASE(test_links_parsing) {
    BOOST_CHECK(libed2k::emule_collection::fromLink(
                    libed2k::url_decode("ed2k://|file|some%5Ffile|100|31D6CFE0D16AE931B73C59D7E0C089C0|/"))
                    .defined());
    BOOST_CHECK(libed2k::emule_collection::fromLink(
                    libed2k::emule_collection::toLink("some_file", 100, libed2k::md4_hash::terminal(), false))
                    .defined());
    BOOST_CHECK(!libed2k::emule_collection::fromLink("ed2k://|file|more3|fd|ggfgfg|/").defined());
    BOOST_CHECK(
        libed2k::emule_collection::fromLink("ed2k://|file|more2|10|DB48A1C00CC972488C29D3FEC9F16A79|/").defined());
    BOOST_CHECK(
        !libed2k::emule_collection::fromLink("ed2k://|file|more1|0|DB48A1C00CC972488C29D3FEC9F16A79|/").defined());
    BOOST_CHECK(libed2k::emule_collection::fromLink("ed2k://|file|Code "
                                                    "Geass.emulecollection|1568|6462EAFF860B98A0592BB0284225F85B|h="
                                                    "52HRRJC7CCJBUZNP5JM6RQWYEDAM3YQM|/")
                    .defined());
    BOOST_CHECK(
        libed2k::emule_collection::fromLink(
            libed2k::url_decode("ed2k://"
                                "%7Cfile%7C%D0%A1%D0%BF%D0%B5%D1%88%D0%B0%D0%BB%D1%8B%20Code%20Geass.emulecollection%"
                                "7C1568%7C6462EAFF860B98A0592BB0284225F85B%7Ch=52HRRJC7CCJBUZNP5JM6RQWYEDAM3YQM%7C/"))
            .defined());
    BOOST_CHECK(
        !libed2k::emule_collection::fromLink(
             libed2k::url_decode("ed2k://"
                                 "%7Cfile%7C%D0%A1%D0%BF%D0%B5%D1%88%D0%B0%D0%BB%D1%8B%20Code%20Geass.emulecollection%"
                                 "7C1568%7C6462EAFF860B98A0592BB0284225F85B%7Ch=52HRRJC7CCJBUZNP5JM6RQWYEDAM3YQM%7C/ "))
             .defined());
}

BOOST_AUTO_TEST_CASE(test_links_generation) {
    BOOST_CHECK_EQUAL(libed2k::emule_collection::toLink("some_file", 100, libed2k::md4_hash::terminal(), false),
                      "ed2k://|file|some_file|100|31D6CFE0D16AE931B73C59D7E0C089C0|/");
    BOOST_CHECK_EQUAL(libed2k::emule_collection::toLink(
                          "xxx.avi", 100, libed2k::md4_hash::fromString("DB48A1C00CC972488C29D3FEC9F16A79"), true),
                      "ed2k://|file|xxx%2Eavi|100|DB48A1C00CC972488C29D3FEC9F16A79|/");
    BOOST_CHECK_EQUAL(libed2k::emule_collection::toLink("some_file", 100, libed2k::md4_hash::terminal(), true),
                      "ed2k://|file|some%5Ffile|100|31D6CFE0D16AE931B73C59D7E0C089C0|/");
}

BOOST_AUTO_TEST_CASE(test_fast_resume_data_serialize) {
    std::vector<char> v(100, 122);
    libed2k::transfer_resume_data trd(libed2k::md4_hash::fromString("DB48A1C00CC972488C29D3FEC9F16A79"),
                                      std::string("xxx.data"), 1009, true, v);
    libed2k::transfer_resume_data trd_dst;
    std::stringstream sstream_out(std::ios::out | std::ios::in | std::ios::binary);
    libed2k::archive::ed2k_oarchive out_string_archive(sstream_out);

    out_string_archive << trd;

    // go to begin
    sstream_out.seekg(0, std::ios::beg);
    libed2k::archive::ed2k_iarchive in_string_archive(sstream_out);
    in_string_archive >> trd_dst;
    BOOST_CHECK(trd_dst.m_seed);
    BOOST_CHECK_EQUAL(1009u, trd_dst.m_filesize);
}

BOOST_AUTO_TEST_CASE(test_incorrect_packet) {
    // hash
    // uint32 size - incorrect size
    char chPacket[] = {'\x11', '\x12', '\x14', '\xFF', '\xEE', '\x10', '\x10', '\x10', '\x10', '\x10', '\x10',
                       '\x10', '\x10', '\x10', '\x10', '\x10', '\xEF', '\x10', '\x1F', '\x10'};  // uint32 huge size

    boost::iostreams::stream_buffer<libed2k::base_connection::Device> buffer(&chPacket[0], sizeof(chPacket));
    std::istream in_array_stream(&buffer);
    libed2k::archive::ed2k_iarchive ia(in_array_stream);

    bool bException = false;

    try {
        libed2k::client_directory_content_result t;
        ia >> t;
    } catch (libed2k::libed2k_exception& e) {
        bException = true;
        BOOST_CHECK(e.error() == static_cast<boost::system::error_code>(libed2k::errors::decode_packet_error));
    }

    BOOST_CHECK(bException);
}

BOOST_AUTO_TEST_CASE(test_incorrect_packet_2) {
    char chPacket[] = {'\x11', '\x12', '\x14', '\xFF', '\xEE', '\x10', '\x10', '\x10', '\x10', '\x10', '\x10',
                       '\x10', '\x10', '\x10', '\x10', '\x10', '\x10', '\x00', '\x00', '\x00'};  // uint32 huge size

    char chPacketCorrect[] = {'\x11', '\x12', '\x14', '\xFF', '\xEE', '\x10', '\x10', '\x10', '\x10', '\x10',
                              '\x10', '\x10', '\x10', '\x10', '\x10', '\x10', '\x00', '\x00', '\x00', '\x00'};

    boost::iostreams::stream_buffer<libed2k::base_connection::Device> buffer(&chPacket[0], sizeof(chPacket));
    std::istream in_array_stream(&buffer);
    libed2k::archive::ed2k_iarchive ia(in_array_stream);
    libed2k::client_directory_content_result t;
    BOOST_CHECK_THROW(ia >> t, libed2k::libed2k_exception);

    boost::iostreams::stream_buffer<libed2k::base_connection::Device> buffer_correct(&chPacketCorrect[0],
                                                                                     sizeof(chPacketCorrect));
    std::istream in_array_stream_corr(&buffer_correct);
    libed2k::archive::ed2k_iarchive ia_corr(in_array_stream_corr);
    BOOST_CHECK_NO_THROW(ia_corr >> t);
}

BOOST_AUTO_TEST_SUITE_END()
