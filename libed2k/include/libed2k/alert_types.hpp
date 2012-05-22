#ifndef __LIBED2K_ALERT_TYPES__
#define __LIBED2K_ALERT_TYPES__

// for print_endpoint
#include <libtorrent/socket.hpp>

#include "libed2k/alert.hpp"
#include "libed2k/types.hpp"
#include "libed2k/error_code.hpp"
#include "libed2k/packet_struct.hpp"


namespace libed2k
{
    struct server_name_resolved_alert : alert
    {
        const static int static_category = alert::status_notification;
        server_name_resolved_alert(const std::string& strServer) : m_strServer(strServer){}
        virtual int category() const { return static_category; }

        virtual std::auto_ptr<alert> clone() const
        {
            return std::auto_ptr<alert>(new server_name_resolved_alert(*this));
        }

        virtual std::string message() const { return std::string("server name was resolved"); }
        virtual char const* what() const { return "server notification"; }

        std::string m_strServer;
    };

    /**
      * emit when server connection was initialized
     */
    struct server_connection_initialized_alert : alert
    {
        const static int static_category = alert::status_notification;

        server_connection_initialized_alert(boost::uint32_t nClientId,
                boost::uint32_t nTCPFlags, boost::uint32_t nAuxPort) :
                    m_nClientId(nClientId),
                    m_nTCPFlags(nTCPFlags),
                    m_nAuxPort(nAuxPort)
        {}

        virtual int category() const { return static_category; }

        virtual std::auto_ptr<alert> clone() const
        {
            return std::auto_ptr<alert>(new server_connection_initialized_alert(*this));
        }

        virtual std::string message() const { return std::string("server connection was initialized"); }
        virtual char const* what() const { return "server notification"; }

        boost::uint32_t m_nClientId;
        boost::uint32_t m_nTCPFlags;
        boost::uint32_t m_nAuxPort;
    };

    /**
      * emit on OP_SERVERSTATUS
     */
    struct server_status_alert : alert
    {
        const static int static_category = alert::status_notification | alert::server_notification;

        server_status_alert(boost::uint32_t nFilesCount, boost::uint32_t nUsersCount) :
            m_nFilesCount(nFilesCount), m_nUsersCount(nUsersCount)
        {
        }

        virtual int category() const { return static_category; }

        virtual std::auto_ptr<alert> clone() const
        {
            return std::auto_ptr<alert>(new server_status_alert(*this));
        }

        virtual std::string message() const { return std::string("server status information"); }
        virtual char const* what() const { return "server status information"; }

        boost::uint32_t m_nFilesCount;
        boost::uint32_t m_nUsersCount;
    };

    /**
      * emit on OP_SERVERIDENT
     */

    struct server_identity_alert : alert
    {
        const static int static_category = alert::status_notification | alert::server_notification;

        server_identity_alert(const md4_hash& hServer , net_identifier address, const std::string& strName, const std::string& strDescr) :
            m_hServer(hServer), m_address(address), m_strName(strName), m_strDescr(strDescr)
        {
        }

        virtual int category() const { return static_category; }

        virtual std::auto_ptr<alert> clone() const
        {
            return std::auto_ptr<alert>(new server_identity_alert(*this));
        }

        virtual std::string message() const { return std::string("server identity information"); }
        virtual char const* what() const { return "server identity information"; }

        md4_hash        m_hServer;
        net_identifier  m_address;
        std::string     m_strName;
        std::string     m_strDescr;
    };

    /**
      * emit for every server message
     */
    struct server_message_alert: alert
    {
        const static int static_category = alert::server_notification;

        server_message_alert(const std::string& strMessage) : m_strMessage(strMessage){}
        virtual int category() const { return static_category; }

        virtual std::string message() const { return m_strMessage; }
        virtual char const* what() const { return "server message incoming"; }

        virtual std::auto_ptr<alert> clone() const
        {
            return (std::auto_ptr<alert>(new server_message_alert(*this)));
        }

        std::string m_strMessage;
    };

    /**
      * emit when server down
     */
    struct server_connection_failed : alert
    {
        const static int static_category = alert::status_notification | alert::server_notification;

        server_connection_failed(const error_code& error) : m_error(error){}
        virtual int category() const { return static_category; }

        virtual std::string message() const { return m_error.message(); }
        virtual char const* what() const { return "server connection failed"; }

        virtual std::auto_ptr<alert> clone() const
        {
            return (std::auto_ptr<alert>(new server_connection_failed(*this)));
        }

        error_code m_error;
    };

    struct search_result_alert : alert
    {
        const static int static_category = alert::server_notification;

        search_result_alert(const search_file_list& plist) : m_list(plist){}
        virtual int category() const { return static_category; }

        virtual std::string message() const { return "search result from string"; }
        virtual char const* what() const { return "search result from server"; }

        virtual std::auto_ptr<alert> clone() const
        {
            return (std::auto_ptr<alert>(new search_result_alert(*this)));
        }

        search_file_list    m_list;
    };

    struct mule_listen_failed_alert: alert
    {
        mule_listen_failed_alert(tcp::endpoint const& ep, error_code const& ec):
            endpoint(ep), error(ec)
        {}

        tcp::endpoint endpoint;
        error_code error;

        virtual std::auto_ptr<alert> clone() const
        { return std::auto_ptr<alert>(new mule_listen_failed_alert(*this)); }
        virtual char const* what() const { return "listen failed"; }
        const static int static_category = alert::status_notification | alert::error_notification;
        virtual int category() const { return static_category; }
        virtual std::string message() const
        {
            char ret[200];
            snprintf(ret, sizeof(ret), "mule listening on %s failed: %s"
                , libtorrent::print_endpoint(endpoint).c_str(), error.message().c_str());
            return ret;
        }

    };

}


#endif //__LIBED2K_ALERT_TYPES__