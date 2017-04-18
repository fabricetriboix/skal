/* Copyright (c) 2017  Fabrice Triboix
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "skal-cpp.hpp"
#include "skal.h"
#include <exception>
#include <alloca.h>


namespace skal {



/*------------+
 | Allocators |
 +------------*/


/* Hooks to allocator functions
 *
 * NB: We can put these functions in an unnamed namespace or declare them
 * static. They need to have "extern" linkage scope in order for the "friend"
 * declarations of the `skal::Allocator` class to work.
 */

void* skalCppAllocatorAllocate(void* cookie, const char* id, int64_t size_B)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != nullptr);
    std::string tmp;
    if (id != nullptr) {
        tmp = id;
    }
    return allocator->allocate(tmp, size_B);
}

void skalCppAllocatorDeallocate(void* cookie, void* obj)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != nullptr);
    allocator->deallocate(obj);
}

void* skalCppAllocatorMap(void* cookie, void* obj)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != nullptr);
    return allocator->map(obj);
}

void skalCppAllocatorUnmap(void* cookie, void* obj)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != nullptr);
    allocator->unmap(obj);
}


Allocator::Allocator(const std::string& name, Scope scope)
{
    int n = snprintf(mAllocator.name, sizeof(mAllocator.name),
            "%s", name.c_str());
    SKALASSERT(n < (int)sizeof(mAllocator.name));

    switch (scope) {
    case Allocator::Scope::THREAD :
        mAllocator.scope = SKAL_ALLOCATOR_SCOPE_THREAD;
        break;
    case Allocator::Scope::PROCESS :
        mAllocator.scope = SKAL_ALLOCATOR_SCOPE_PROCESS;
        break;
    case Allocator::Scope::COMPUTER :
        mAllocator.scope = SKAL_ALLOCATOR_SCOPE_COMPUTER;
        break;
    case Allocator::Scope::SYSTEM :
        mAllocator.scope = SKAL_ALLOCATOR_SCOPE_SYSTEM;
        break;
    default :
        SKALPANIC_MSG("Invalid allocator scope: %d", (int)scope);
    }

    mAllocator.allocate = skalCppAllocatorAllocate;
    mAllocator.deallocate = skalCppAllocatorDeallocate;
    mAllocator.map = skalCppAllocatorMap;
    mAllocator.unmap = skalCppAllocatorUnmap;
    mAllocator.cookie = this;
}

Allocator::~Allocator()
{
}


/*--------+
 | Alarms |
 +--------*/

Alarm::Alarm(const std::string& name, SeverityE severity,
        bool isOn, bool autoOff, const std::string& comment)
    : mAlarm(nullptr)
{
    if (comment.size() == 0) {
        mAlarm = SkalAlarmCreate(name.c_str(), (SkalAlarmSeverityE)severity,
                isOn, autoOff, nullptr);
    } else {
        mAlarm = SkalAlarmCreate(name.c_str(), (SkalAlarmSeverityE)severity,
                isOn, autoOff, "%s", comment.c_str());
    }
}

Alarm::Alarm(SkalAlarm* alarm) : mAlarm(alarm)
{
}

Alarm::~Alarm()
{
    if (mAlarm != nullptr) {
        SkalAlarmUnref(mAlarm);
    }
}

std::string Alarm::Name() const
{
    return SkalAlarmName(mAlarm);
}

Alarm::SeverityE Alarm::Severity() const
{
    return (Alarm::SeverityE)SkalAlarmSeverity(mAlarm);
}

bool Alarm::IsOn() const
{
    return SkalAlarmIsOn(mAlarm);
}

bool Alarm::AutoOff() const
{
    return SkalAlarmAutoOff(mAlarm);
}

std::string Alarm::Comment() const
{
    const char* comment = SkalAlarmComment(mAlarm);
    if (nullptr == comment) {
        comment = "";
    }
    return comment;
}

std::string Alarm::Origin() const
{
    const char* origin = SkalAlarmOrigin(mAlarm);
    if (nullptr == origin) {
        origin = "";
    }
    return origin;
}

int64_t Alarm::Timestamp_us() const
{
    return SkalAlarmTimestamp_us(mAlarm);
}


/*-------+
 | Blobs |
 +-------*/

Blob::Blob(SkalBlob* blob) : mBlob(blob)
{
}

Blob::~Blob()
{
    SkalBlobUnref(mBlob);
}

std::string Blob::Id() const
{
    const char* id = SkalBlobId(mBlob);
    if (nullptr == id) {
        id = "";
    }
    return id;
}

std::string Blob::Name() const
{
    const char* name = SkalBlobName(mBlob);
    if (nullptr == name) {
        name = "";
    }
    return name;
}

int64_t Blob::Size_B() const
{
    return SkalBlobSize_B(mBlob);
}

Blob::ScopedMap::ScopedMap(Blob& blob)
    : mBlob(&blob), mPtr(SkalBlobMap(blob.mBlob))
{
    if (nullptr == mPtr) {
        throw std::runtime_error("Failed to map blob");
    }
}

Blob::ScopedMap::~ScopedMap()
{
    if (mPtr != nullptr) {
        SkalBlobUnmap(mBlob->mBlob);
    }
}

void* Blob::ScopedMap::Get() const
{
    return mPtr;
}

std::shared_ptr<Blob> CreateBlob(const std::string& allocator,
        const std::string& id, const std::string& name, int64_t size_B)
{
    const char* tmpAllocator = nullptr;
    if (allocator.size() > 0) {
        tmpAllocator = allocator.c_str();
    }
    const char* tmpId = nullptr;
    if (id.size() > 0) {
        tmpId = id.c_str();
    }
    const char* tmpName = nullptr;
    if (name.size() > 0) {
        tmpName = name.c_str();
    }
    SkalBlob* blob = SkalBlobCreate(tmpAllocator, tmpId, tmpName, size_B);
    if (nullptr == blob) {
        return nullptr;
    }
    return std::shared_ptr<Blob>(new Blob(blob));
}


/*----------+
 | Messages |
 +----------*/

Msg::Msg(const std::string& name, const std::string& recipient,
        uint8_t flags, int8_t ttl)
{
    mMsg = SkalMsgCreateEx(name.c_str(), recipient.c_str(), flags, ttl);
}

Msg::~Msg()
{
    SkalMsgUnref(mMsg);
}

void Msg::DecrementTtl()
{
    SkalMsgDecrementTtl(mMsg);
}

void Msg::AddField(const std::string& name, int64_t i)
{
    SkalMsgAddInt(mMsg, name.c_str(), i);
}

void Msg::AddField(const std::string& name, double d)
{
    SkalMsgAddDouble(mMsg, name.c_str(), d);
}

void Msg::AddField(const std::string& name, const std::string& str)
{
    SkalMsgAddString(mMsg, name.c_str(), str.c_str());
}

void Msg::AddField(const std::string& name, const uint8_t* miniblob, int size_B)
{
    SkalMsgAddMiniblob(mMsg, name.c_str(), miniblob, size_B);
}

void Msg::AttachBlob(const std::string& name, std::shared_ptr<Blob> blob)
{
    SkalMsgAttachBlob(mMsg, name.c_str(), blob->mBlob);
}

bool Msg::HasField(const std::string& name)
{
    return SkalMsgHasField(mMsg, name.c_str());
}

int64_t Msg::GetIntField(const std::string& name) const
{
    return SkalMsgGetInt(mMsg, name.c_str());
}

double Msg::GetDoubleField(const std::string& name) const
{
    return SkalMsgGetDouble(mMsg, name.c_str());
}

std::string Msg::GetStringField(const std::string& name) const
{
    return SkalMsgGetString(mMsg, name.c_str());
}

const uint8_t* Msg::GetMiniblobField(const std::string& name, int& size_B) const
{
    return SkalMsgGetMiniblob(mMsg, name.c_str(), &size_B);
}

std::shared_ptr<Blob> Msg::GetBlob(const std::string& name) const
{
    SkalBlob* blob = SkalMsgGetBlob(mMsg, name.c_str());
    return std::shared_ptr<Blob>(new Blob(blob));
}

void Msg::AttachAlarm(std::shared_ptr<Alarm> alarm)
{
    SkalMsgAttachAlarm(mMsg, alarm->mAlarm);
}

std::shared_ptr<Alarm> Msg::DetachAlarm() const
{
    SkalAlarm* alarm = SkalMsgDetachAlarm(mMsg);
    return std::shared_ptr<Alarm>(new Alarm(alarm));
}

std::string Msg::Name() const
{
    return SkalMsgName(mMsg);
}

std::string Msg::Sender() const
{
    return SkalMsgSender(mMsg);
}

std::string Msg::Recipient() const
{
    return SkalMsgRecipient(mMsg);
}

uint8_t Msg::Flags() const
{
    return SkalMsgFlags(mMsg);
}

int8_t Msg::Ttl() const
{
    return SkalMsgTtl(mMsg);
}

Msg::Msg(SkalMsg* msg) : mMsg(msg)
{
}

void Send(std::unique_ptr<Msg> msg)
{
    // NB: `SkalMsgSend()` takes ownership of the message, so we need to
    // reference it first, so our `msg` object destructs properly.
    SkalMsgRef(msg->mMsg);
    SkalMsgSend(msg->mMsg);
}


/*---------+
 | Threads |
 +---------*/

class Thread {
public :
    Thread(std::function<bool(Msg&)> processMsg) : mProcessMsg(processMsg)
    {
    }

    std::function<bool(Msg&)> mProcessMsg;
};

/* Hook to thread's message processing function */
bool skalCppProcessMsg(void* cookie, SkalMsg* skalmsg)
{
    Thread* t = (Thread*)cookie;
    Msg msg(skalmsg);
    bool ok = t->mProcessMsg(msg);
    if (!ok) {
        delete t;
    }
    return ok;
}

void CreateThread(const std::string& name, std::function<bool(Msg&)> processMsg,
            int64_t queueThreshold, int32_t stackSize_B, int64_t xoffTimeout_us)
{
    Thread* t = new Thread(processMsg);

    SkalThreadCfg cfg;
    int n = snprintf(cfg.name, sizeof(cfg.name), "%s", name.c_str());
    SKALASSERT(n < (int)sizeof(cfg.name));
    cfg.processMsg = skalCppProcessMsg;
    cfg.cookie = t;
    cfg.queueThreshold = queueThreshold;
    cfg.stackSize_B = stackSize_B;
    cfg.xoffTimeout_us = xoffTimeout_us;
    cfg.statsCount = 0;
    SkalThreadCreate(&cfg);
}

void CreateThread(const std::string& name, std::function<bool(Msg&)> processMsg)
{
    CreateThread(name, processMsg, 0, 0, 0);
}


/*------------------+
 | Global functions |
 +------------------*/

bool Init(const char* skaldUrl,
        std::vector<std::shared_ptr<Allocator>> allocators)
{
    size_t n = allocators.size();
    SkalAllocator* a = (SkalAllocator*)alloca(sizeof(*a) * n);
    SKALASSERT(a != nullptr);
    for (size_t i = 0; i < n; ++i) {
        a[i] = allocators[i]->mAllocator;
    }
    return SkalInit(skaldUrl, a, n);
}

bool Init(const char* skaldUrl)
{
    std::vector<std::shared_ptr<Allocator>> empty;
    return Init(skaldUrl, empty);
}

bool Init()
{
    return Init(nullptr);
}

void Exit()
{
    SkalExit();
}

std::string Domain()
{
    return SkalDomain();
}

bool Pause()
{
    return SkalPause();
}

void Cancel()
{
    SkalCancel();
}


} // namespace skal
