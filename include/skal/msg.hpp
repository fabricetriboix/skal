/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/global.hpp>
#include <skal/alarm.hpp>
#include <skal/blob.hpp>
#include <utility>
#include <vector>
#include <map>
#include <memory>
#include <boost/optional.hpp>

namespace skal {

/** Exception class: received a message that can't be parsed */
struct bad_msg_format : public error
{
    bad_msg_format() : error("skal::bad_msg_format") { }
};

/** Exception class: received a message with an unsupported version number */
struct bad_msg_version : public error
{
    bad_msg_version() : error("skal::bad_msg_version") { }
};

/** Miniblob class: just a bunch of bytes */
typedef std::vector<uint8_t> miniblob_t;

/** Class that represents a message */
class msg_t final
{
public :
    msg_t() = delete;
    ~msg_t() = default;
    msg_t& operator=(const msg_t&) = delete;

    struct flag_t final
    {
        /** Message flag: this message is urgent
         *
         * This message will jump in front of regular messages when enqueued at
         * the various hops.
         *
         * The default is to send non-urgent messages. Please use this flag
         * sparringly. This feature is implemented on a "best-effort" basis
         * only, there are no guarantee that this message will actually arrive
         * before any regular message sent previously.
         */
        constexpr static uint32_t urgent = 0x01;
    };

    /** Constructor
     *
     * \param sender    [in] Name of worker which created this message; empty
     *                       string if message created outside a skal worker
     * \param recipient [in] Whom to send this message to; this is the name of
     *                       a worker or a multicast group
     * \param action    [in] Message action; must not be an empty string;
     *                       please note that message actions starting with
     *                       "skal" are reserved for skal's own use
     * \param flags     [in] Message flag; please refer to `flag_t`
     * \param ttl       [in] Time-to-live counter initial value
     */
    msg_t(std::string sender, std::string recipient, std::string action,
            uint32_t flags = 0, int8_t ttl = default_ttl)
        : msg_t(std::move(sender), std::move(recipient), std::move(action),
                flags, 0, ttl)
    {
    }

    /** Constuctor where the sender is set automatically
     *
     * The sender will be set to the name of the calling worker, or the empty
     * string if this function is called from outside the skal framework.
     *
     * \param recipient [in] Whom to send this message to; this is the name of
     *                       a worker or a multicast group (in which case the
     *                       `flag_t::multicast` flag must also be set)
     * \param action    [in] Message action; must not be an empty string;
     *                       please note that message actions starting with
     *                       "skal" are reserved for skal's own use
     * \param flags     [in] Message flag; please refer to `flag_t`
     * \param ttl       [in] Time-to-live counter initial value; <= for default
     */
    msg_t(std::string recipient, std::string action,
            uint32_t flags = 0, int8_t ttl = default_ttl)
        : msg_t(me(), std::move(recipient), std::move(action), flags, 0, ttl)
    {
    }

    /** Construct a message from a serialized form
     *
     * \param data [in] Serialized form of the message
     *
     * \throw `bad_msg_format` if `data` is not valid
     *
     * \throw `bad_msg_version` if `data` encodes a message of an unsupported
     *        message version
     *
     * \throw `std::out_of_range` if an attached blob refers to a non-existent
     *        allocator
     *
     * \throw `bad_blob` if an attached blob can't be opened
     */
    explicit msg_t(std::string data);

    /** Copy constructor
     *
     * You must not call this constructor if `right` has any of its blob mapped
     * by the calling thread.
     *
     * \param right [in] Message to copy
     */
    msg_t(const msg_t& right);

    /** Constructor from a pointer to a message */
    msg_t(const std::unique_ptr<msg_t>& right) : msg_t(*right.get())
    {
    }

    /** Factory function to create a msg */
    static std::unique_ptr<msg_t> create(std::string sender,
            std::string recipient, std::string action,
            uint32_t flags = 0, int8_t ttl = default_ttl);

    /** Factory function to create a msg with automatic sender
     *
     * The sender will be set to the calling worker, or the empty string if
     * called from outside the skal framework.
     */
    static std::unique_ptr<msg_t> create(std::string recipient,
            std::string action, uint32_t flags = 0, int8_t ttl = default_ttl);

    /** Factory function to create a msg from a serialized form */
    static std::unique_ptr<msg_t> create(std::string data);

    /** Get the timestamp of when this message had been created
     *
     * \return Message timestamp
     */
    timestamp_t timestamp() const
    {
        return timestamp_;
    }

    const std::string& sender() const
    {
        return sender_;
    }

    const std::string& recipient() const
    {
        return recipient_;
    }

    const std::string& action() const
    {
        return action_;
    }

    uint32_t flags() const
    {
        return flags_;
    }

    void flags(uint32_t value)
    {
        flags_ = value;
    }

    int8_t ttl() const
    {
        return ttl_;
    }

    int8_t decrement_ttl()
    {
        return --ttl_;
    }

    /** Attach an alarm to the message
     *
     * Please note an alarm is not considered a field. They are just stored
     * indiscriminately one after the other.
     *
     * \param alarm [in] Alarm to attach
     */
    void attach_alarm(alarm_t alarm)
    {
        alarms_.push_back(std::move(alarm));
    }

    /** Detach an alarm from the message
     *
     * If more than one alarm are currently attached to the message, an alarm
     * is selected in an arbitrary fashion to be detached and returned by this
     * function.
     *
     * \return The detached alarm, or `boost::none` if no more alarms.
     */
    boost::optional<alarm_t> detach_alarm();

    /** Add an integer field to the message
     *
     * If a field with that name already exists, it will be overwritten.
     *
     * \param name [in] Name of the integer field
     * \param i    [in] Integer to add
     */
    void add_field(std::string name, int64_t i)
    {
        ints_[std::move(name)] = i;
    }

    void add_field(std::string name, int i)
    {
        add_field(std::move(name), static_cast<int64_t>(i));
    }

    /** Add a floating-point field to the message
     *
     * If a field with that name already exists, it will be overwritten.
     *
     * \param name [in] Name of the floating-point field
     * \param d    [in] Floating-point to add
     */
    void add_field(std::string name, double d)
    {
        doubles_[std::move(name)] = d;
    }

    /** Add a string field to the message
     *
     * If a field with that name already exists, it will be overwritten.
     *
     * \param name [in] Name of the string field
     * \param s    [in] String to add
     */
    void add_field(std::string name, std::string s)
    {
        strings_[std::move(name)] = std::move(s);
    }

    /** Add a miniblob field to the message
     *
     * If a field with that name already exists, it will be overwritten.
     *
     * \param name     [in] Name of the miniblob field
     * \param miniblob [in] Miniblob to add
     */
    void add_field(std::string name, miniblob_t miniblob)
    {
        miniblobs_[std::move(name)] = std::move(miniblob);
    }

    /** Add a blob field to the message
     *
     * If a field with that name already exists, it will be overwritten.
     *
     * \param name  [in] Name of the miniblob field
     * \param proxy [in] Proxy to the blob to add
     */
    void add_field(std::string name, blob_proxy_t proxy)
    {
        blobs_[std::move(name)] = std::move(proxy);
    }

    /** Check if the given integer field exists
     *
     * \param name [in] Name of the field to check
     *
     * \return `true` if the field exists, `false` if not
     */
    bool has_int(const std::string& name) const
    {
        return ints_.find(name) != ints_.end();
    }

    /** Get an integer field
     *
     * \param name [in] Name of field to lookup
     *
     * \return The integer value
     *
     * \throw `std::out_of_range` if there is no integer field with that `name`
     */
    int64_t get_int(const std::string& name) const
    {
        return ints_.at(name);
    }

    /** Check if the given double field exists
     *
     * \param name [in] Name of the field to check
     *
     * \return `true` if the field exists, `false` if not
     */
    bool has_double(const std::string& name) const
    {
        return doubles_.find(name) != doubles_.end();
    }

    /** Get a floating-point field
     *
     * \param name [in] Name of field to lookup
     *
     * \return The double value
     *
     * \throw `std::out_of_range` if there is no floating-point field with
     *         that `name`
     */
    double get_double(const std::string& name) const
    {
        return doubles_.at(name);
    }

    /** Check if the given string field exists
     *
     * \param name [in] Name of the field to check
     *
     * \return `true` if the field exists, `false` if not
     */
    bool has_string(const std::string& name) const
    {
        return strings_.find(name) != strings_.end();
    }

    /** Get a string field
     *
     * \param name [in] String field to lookup
     *
     * \return The string value
     *
     * \throw `std::out_of_range` if there is no string field with that `name`
     */
    const std::string& get_string(const std::string& name) const
    {
        return strings_.at(name);
    }

    /** Check if the given miniblob field exists
     *
     * \param name [in] Name of the field to check
     *
     * \return `true` if the field exists, `false` if not
     */
    bool has_miniblob(const std::string& name) const
    {
        return miniblobs_.find(name) != miniblobs_.end();
    }

    /** Get a miniblob field
     *
     * \param name [in] Miniblob field to lookup
     *
     * \return The miniblob value
     *
     * \throw `std::out_of_range` if there is no miniblob field with that
     *        `name`
     */
    const miniblob_t& get_miniblob(const std::string& name) const
    {
        return miniblobs_.at(name);
    }

    /** Check if the given blob field exists
     *
     * \param name [in] Name of the field to check
     *
     * \return `true` if the field exists, `false` if not
     */
    bool has_blob(const std::string& name) const
    {
        return blobs_.find(name) != blobs_.end();
    }

    /** Get a blob field
     *
     * \param name [in] Blob field to lookup
     *
     * \return A proxy to the blob
     *
     * \throw `std::out_of_range` if there is no blob field with that `name`
     */
    blob_proxy_t get_blob(const std::string& name) const
    {
        return blobs_.at(name);
    }

    /** Detach a blob field
     *
     * This function is similar to `get_blob()`, but it removes the blob proxy
     * from the message, effectively transferring the blob proxy from the
     * message to you.
     *
     * A subsequent call to `detach_blob()` with the same `name` will throw
     * `std::out_of_range` because the blob proxy has been removed from the
     * message object.
     *
     * \param name [in] Blob field to lookup
     *
     * \return A proxy to the blob
     *
     * \throw `std::out_of_range` if there is no field with that `name`
     */
    blob_proxy_t detach_blob(const std::string& name)
    {
        blob_proxy_t proxy = std::move(blobs_.at(name));
        blobs_.erase(name);
        return proxy;
    }

    /** Serialize the message
     *
     * The returned value is an `std::string`, but it does not represent a
     * human-readable string. It's just used as a container of bytes (that's
     * how google protobuf does it...)
     *
     * \return The message in serialized form
     */
    std::string serialize() const;

private :
    struct iflag_t final
    {
        /** Internal message flag: this is an internal message */
        constexpr static uint32_t internal = 0x10000;
    };

    uint32_t iflags() const
    {
        return iflags_;
    }

    void iflags(uint32_t flags)
    {
        iflags_ = flags;
    }

    void set_iflag(uint32_t flags)
    {
        iflags_ |= flags;
    }

    void reset_iflag(uint32_t flags)
    {
        iflags_ &= ~flags;
    }

    /** Set the sender */
    void sender(std::string sender);

    /** Set the recipient */
    void recipient(std::string recipient);

    /** Full constructor (with internal flags) */
    msg_t(std::string sender, std::string recipient, std::string action,
            uint32_t flags, uint32_t iflags, int8_t ttl);

    /** Factory function to create an internal msg */
    static std::unique_ptr<msg_t> create_internal(std::string sender,
            std::string recipient, std::string action,
            uint32_t flags = 0, int8_t ttl = default_ttl);

    /** Factory function to create an internal msg with automatic sender */
    static std::unique_ptr<msg_t> create_internal(std::string recipient,
            std::string action, uint32_t flags = 0, int8_t ttl = default_ttl);

    timestamp_t          timestamp_;
    std::string          sender_;
    std::string          recipient_;
    std::string          action_;
    uint32_t             flags_;
    uint32_t             iflags_;
    int8_t               ttl_;
    std::vector<alarm_t> alarms_;
    std::map<std::string, int64_t>      ints_;
    std::map<std::string, double>       doubles_;
    std::map<std::string, std::string>  strings_;
    std::map<std::string, miniblob_t>   miniblobs_;
    std::map<std::string, blob_proxy_t> blobs_;

    friend class queue_t;
    friend class worker_t;
    friend class group_t;
};

/** Version number for the message format */
constexpr const uint8_t msg_version = 1;

} // namespace skal
