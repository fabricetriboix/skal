/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/msg.hpp>
#include <skal/global.hpp>
#include <skal/log.hpp>
#include <skal/util.hpp>
#include "msg.pb.h"
#include <cstring>

namespace skal {

const uint32_t msg_t::flag_t::urgent;

const uint32_t msg_t::iflag_t::internal;

msg_t::msg_t(std::string sender, std::string recipient, std::string action,
        uint32_t flags, uint32_t iflags, int8_t ttl)
    : timestamp_(std::chrono::system_clock::now())
    , sender_(full_name(std::move(sender)))
    , recipient_(full_name(std::move(recipient)))
    , action_(std::move(action))
    , flags_(flags)
    , iflags_(iflags)
    , ttl_(ttl)
{
}

std::unique_ptr<msg_t> msg_t::create(std::string sender, std::string recipient,
        std::string action, uint32_t flags, int8_t ttl)
{
    return std::make_unique<msg_t>(std::move(sender), std::move(recipient),
            std::move(action), flags, ttl);
}

std::unique_ptr<msg_t> msg_t::create(std::string recipient, std::string action,
        uint32_t flags, int8_t ttl)
{
    return std::make_unique<msg_t>(std::move(recipient), std::move(action),
            flags, ttl);
}

std::unique_ptr<msg_t> msg_t::create(std::string data)
{
    return std::make_unique<msg_t>(std::move(data));
}

std::unique_ptr<msg_t> msg_t::create_internal(std::string sender,
        std::string recipient, std::string action, uint32_t flags, int8_t ttl)
{
    return std::unique_ptr<msg_t>(new msg_t(std::move(sender),
                std::move(recipient), std::move(action),
                flags, iflag_t::internal, ttl));
}

std::unique_ptr<msg_t> msg_t::create_internal(std::string recipient,
        std::string action, uint32_t flags, int8_t ttl)
{
    return std::unique_ptr<msg_t>(new msg_t(me(), std::move(recipient),
                std::move(action), flags, iflag_t::internal, ttl));
}

msg_t::msg_t(std::string data)
{
    Msg tmp;
    if (!tmp.ParseFromString(data)) {
        skal_log(warning) << "Failed to parse message";
        throw bad_msg_format();
    }

    if (!tmp.has_version()) {
        skal_log(warning) << "Received a message without version number";
        throw bad_msg_format();
    }
    if (tmp.version() != msg_version) {
        skal_log(warning) << "Received a message with version " <<
            tmp.version() << "; I only support " << msg_version;
        throw bad_msg_version();
    }
    if (!tmp.has_timestamp() || !tmp.has_sender() || !tmp.has_recipient()
            || !tmp.has_action() || !tmp.has_ttl()) {
        skal_log(warning)
            << "Received a message that is missing required fields";
        throw bad_msg_format();
    }

    timestamp_ = timestamp_t(std::chrono::nanoseconds(tmp.timestamp()));
    sender_ = full_name(tmp.sender());
    recipient_ = full_name(tmp.recipient());
    action_ = tmp.action();
    flags_ = tmp.flags();
    iflags_ = tmp.iflags();
    ttl_ = tmp.ttl();

    for (int i = 0; i < tmp.alarms_size(); ++i) {
        const Alarm& tmp_alarm = tmp.alarms(i);
        if (!tmp_alarm.has_name() || !tmp_alarm.has_severity()
                || !tmp_alarm.has_is_on() || !tmp_alarm.has_auto_off()
                || !tmp_alarm.has_origin() || !tmp_alarm.has_timestamp()) {
            skal_log(warning)
                << "Received a message with an alarm that is missing required fields";
            throw bad_msg_format();
        }

        alarm_t::severity_t severity;
        switch (tmp_alarm.severity()) {
        case Alarm::NOTICE :
            severity = alarm_t::severity_t::notice;
            break;
        case Alarm::WARNING :
            severity = alarm_t::severity_t::warning;
            break;
        case Alarm::ERROR :
            severity = alarm_t::severity_t::error;
            break;
        default :
            skal_log(warning)
                << "Received a message with an alarm with an invalid severity: "
                << static_cast<int>(tmp_alarm.severity());
            throw bad_msg_format();
        }

        alarm_t alarm(tmp_alarm.name(), tmp_alarm.origin(), severity,
                tmp_alarm.is_on(), tmp_alarm.auto_off(), tmp_alarm.note(),
                timestamp_t(std::chrono::nanoseconds(tmp_alarm.timestamp())));
        alarms_.push_back(alarm);
    } // for each alarm

    for (int i = 0; i < tmp.int_fields_size(); ++i) {
        const IntField& field = tmp.int_fields(i);
        ints_[field.name()] = field.value();
    }
    for (int i = 0; i < tmp.double_fields_size(); ++i) {
        const DoubleField& field = tmp.double_fields(i);
        doubles_[field.name()] = field.value();
    }
    for (int i = 0; i < tmp.string_fields_size(); ++i) {
        const StringField& field = tmp.string_fields(i);
        strings_[field.name()] = field.value();
    }
    for (int i = 0; i < tmp.miniblob_fields_size(); ++i) {
        const MiniblobField& field = tmp.miniblob_fields(i);
        const std::string& value = field.value();
        miniblob_t miniblob;
        miniblob.reserve(value.size());
        std::memcpy(miniblob.data(), value.data(), value.size());
        miniblobs_[field.name()] = std::move(miniblob);
    }
    for (int i = 0; i < tmp.blob_fields_size(); ++i) {
        const BlobField& field = tmp.blob_fields(i);
        blobs_[field.name()] = open_blob(field.allocator(), field.id());
    }
}

msg_t::msg_t(const msg_t& right)
    : timestamp_(right.timestamp_)
    , sender_(right.sender_)
    , recipient_(right.recipient_)
    , action_(right.action_)
    , flags_(right.flags_)
    , iflags_(right.iflags_)
    , ttl_(right.ttl_)
    , alarms_(right.alarms_)
    , ints_(right.ints_)
    , doubles_(right.doubles_)
    , strings_(right.strings_)
    , miniblobs_(right.miniblobs_)
    , blobs_(right.blobs_)
{
}

boost::optional<alarm_t> msg_t::detach_alarm()
{
    if (alarms_.empty()) {
        return boost::none;
    }
    alarm_t alarm = alarms_.back();
    alarms_.pop_back();
    return alarm;
}

std::string msg_t::serialize() const
{
    Msg tmp;
    tmp.set_version(msg_version);
    std::chrono::nanoseconds duration = timestamp_.time_since_epoch();
    tmp.set_timestamp(duration.count());
    tmp.set_sender(sender_);
    tmp.set_recipient(recipient_);
    tmp.set_action(action_);
    tmp.set_flags(flags_);
    tmp.set_iflags(iflags_);
    tmp.set_ttl(ttl_);

    for (auto& alarm : alarms_) {
        Alarm* tmp_alarm = tmp.add_alarms();

        switch (alarm.severity()) {
        case alarm_t::severity_t::notice :
            tmp_alarm->set_severity(Alarm::NOTICE);
            break;
        case alarm_t::severity_t::warning :
            tmp_alarm->set_severity(Alarm::WARNING);
            break;
        case alarm_t::severity_t::error :
            tmp_alarm->set_severity(Alarm::ERROR);
            break;
        default :
            skal_panic() << "Bad alarm severity: "
                << static_cast<int>(alarm.severity());
        }
        tmp_alarm->set_name(alarm.name());
        tmp_alarm->set_is_on(alarm.is_on());
        tmp_alarm->set_auto_off(alarm.auto_off());
        tmp_alarm->set_note(alarm.note());
        tmp_alarm->set_origin(alarm.origin());
        duration = alarm.timestamp().time_since_epoch();
        tmp_alarm->set_timestamp(duration.count());
    } // for each alarm

    for (auto& it : ints_) {
        IntField* field = tmp.add_int_fields();
        field->set_name(it.first);
        field->set_value(it.second);
    }
    for (auto& it : doubles_) {
        DoubleField* field = tmp.add_double_fields();
        field->set_name(it.first);
        field->set_value(it.second);
    }
    for (auto& it : strings_) {
        StringField* field = tmp.add_string_fields();
        field->set_name(it.first);
        field->set_value(it.second);
    }
    for (auto& it : miniblobs_) {
        MiniblobField* field = tmp.add_miniblob_fields();
        field->set_name(it.first);
        field->set_value(it.second.data(), it.second.size());
    }
    for (auto& it : blobs_) {
        BlobField* field = tmp.add_blob_fields();
        field->set_name(it.first);
        field->set_allocator(it.second.allocator().name());
        field->set_id(it.second.id());
    }

    std::string output;
    bool serialized = tmp.SerializeToString(&output);
    skal_assert(serialized);
    return output;
}

void msg_t::sender(std::string sender)
{
    sender_ = full_name(std::move(sender));
}

void msg_t::recipient(std::string recipient)
{
    recipient_ = full_name(std::move(recipient));
}

} // namespace skal
