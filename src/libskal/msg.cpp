/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/msg.hpp>
#include <internal/msg.hpp>
#include <internal/domain.hpp>
#include <skal/detail/log.hpp>
#include <skal/detail/util.hpp>
#include "msg.pb.h"
#include <cstring>

namespace skal {

const uint32_t flag_t::out_of_order_ok;
const uint32_t flag_t::drop_ok;
const uint32_t flag_t::udp;
const uint32_t flag_t::ntf_drop;
const uint32_t flag_t::urgent;
const uint32_t flag_t::multicast;

const uint32_t iflag_t::internal;

msg_t::msg_t(std::string sender, std::string recipient, std::string action,
        uint32_t flags, int8_t ttl)
    : timestamp_(boost::posix_time::microsec_clock::universal_time())
    , sender_(worker_name(std::move(sender)))
    , recipient_(worker_name(std::move(recipient)))
    , action_(std::move(action))
    , flags_(flags)
    , ttl_(ttl)
{
    if (sender_.empty()) {
        sender_ = worker_name("skal-external");
    }
}

msg_t::msg_t(std::string data)
{
    Msg tmp;
    if (!tmp.ParseFromString(data)) {
        SKAL_LOG(warning) << "Failed to parse message";
        throw bad_msg_format();
    }

    if (tmp.version() != msg_version) {
        SKAL_LOG(warning) << "Received a message with version " <<
            tmp.version() << "; I only support " << msg_version;
        throw bad_msg_version();
    }
    if (!tmp.has_timestamp() || !tmp.has_sender() || !tmp.has_recipient()
            || !tmp.has_action() || !tmp.has_ttl())
    {
        SKAL_LOG(warning)
            << "Received a message that is missing required fields";
        throw bad_msg_format();
    }

    timestamp_ = us_to_ptime(tmp.timestamp());
    sender_ = tmp.sender();
    recipient_ = tmp.recipient();
    action_ = tmp.action();
    flags_ = tmp.flags();
    iflags_ = tmp.iflags();
    ttl_ = tmp.ttl();

    for (int i = 0; i < tmp.alarms_size(); ++i) {
        const Alarm& tmp_alarm = tmp.alarms(i);

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
            SKAL_LOG(warning)
                << "Received a message with an alarm with an invalid severity: "
                << static_cast<int>(tmp_alarm.severity());
            throw bad_msg_format();
        }

        alarm_t alarm(tmp_alarm.name(), tmp_alarm.origin(), severity,
                tmp_alarm.is_on(), tmp_alarm.auto_off(), tmp_alarm.msg(),
                us_to_ptime(tmp_alarm.timestamp()));
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
    tmp.set_timestamp(ptime_to_us(timestamp_));
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
            skal_assert(false) << "Bad alarm severity: "
                << static_cast<int>(alarm.severity());
        }
        tmp_alarm->set_name(alarm.name());
        tmp_alarm->set_is_on(alarm.is_on());
        tmp_alarm->set_auto_off(alarm.auto_off());
        tmp_alarm->set_msg(alarm.msg());
        tmp_alarm->set_origin(alarm.origin());
        tmp_alarm->set_timestamp(ptime_to_us(alarm.timestamp()));
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
    skal_assert(tmp.SerializeToString(&output));
    return output;
}

void msg_t::sender(std::string sender)
{
    sender_ = worker_name(std::move(sender));
}

} // namespace skal
