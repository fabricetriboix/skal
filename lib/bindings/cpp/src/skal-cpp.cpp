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
    SKALASSERT(allocator != NULL);
    return allocator->allocate(id, size_B);
}

void skalCppAllocatorDeallocate(void* cookie, void* obj)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != NULL);
    allocator->deallocate(obj);
}

void* skalCppAllocatorMap(void* cookie, void* obj)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != NULL);
    return allocator->map(obj);
}

void skalCppAllocatorUnmap(void* cookie, void* obj)
{
    Allocator* allocator = (Allocator*)cookie;
    SKALASSERT(allocator != NULL);
    allocator->unmap(obj);
}


Allocator::Allocator(const char* name, Scope scope)
{
    SKALASSERT(SkalIsAsciiString(name, sizeof(mAllocator.name)));
    int n = snprintf(mAllocator.name, sizeof(mAllocator.name), "%s", name);
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

Alarm::Alarm(const char* name, SeverityE severity, bool isOn, bool autoOff,
        const char* comment)
    : mAlarm(NULL)
{
    if (NULL == comment) {
        mAlarm = SkalAlarmCreate(name, (SkalAlarmSeverityE)severity,
                isOn, autoOff, NULL);
    } else {
        mAlarm = SkalAlarmCreate(name, (SkalAlarmSeverityE)severity,
                isOn, autoOff, "%s", comment);
    }
}

Alarm::Alarm(SkalAlarm* alarm) : mAlarm(alarm)
{
}

Alarm::~Alarm()
{
    if (mAlarm != NULL) {
        SkalAlarmUnref(mAlarm);
    }
}

const char* Alarm::Name() const
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

const char* Alarm::Comment() const
{
    return SkalAlarmComment(mAlarm);
}

const char* Alarm::Origin() const
{
    return SkalAlarmOrigin(mAlarm);
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

const char* Blob::Id() const
{
    return SkalBlobId(mBlob);
}

const char* Blob::Name() const
{
    return SkalBlobName(mBlob);
}

int64_t Blob::Size_B() const
{
    return SkalBlobSize_B(mBlob);
}

Blob::ScopedMap::ScopedMap(Blob& blob)
    : mBlob(&blob), mPtr(SkalBlobMap(blob.mBlob))
{
    if (NULL == mPtr) {
        throw std::runtime_error("Failed to map blob");
    }
}

Blob::ScopedMap::~ScopedMap()
{
    if (mPtr != NULL) {
        SkalBlobUnmap(mBlob->mBlob);
    }
}

void* Blob::ScopedMap::Get() const
{
    return mPtr;
}

std::shared_ptr<Blob> CreateBlob(const char* allocator,
        const char* id, const char* name, int64_t size_B)
{
    SkalBlob* blob = SkalBlobCreate(allocator, id, name, size_B);
    if (NULL == blob) {
        return NULL;
    }
    return std::shared_ptr<Blob>(new Blob(blob));
}


/*----------+
 | Messages |
 +----------*/

Msg::Msg(const char* name, const char* recipient, uint8_t flags, int8_t ttl)
{
    mMsg = SkalMsgCreateEx(name, recipient, flags, ttl);
}

Msg::~Msg()
{
    SkalMsgUnref(mMsg);
}

void Msg::DecrementTtl()
{
    SkalMsgDecrementTtl(mMsg);
}

void Msg::AddField(const char* name, int64_t i)
{
    SkalMsgAddInt(mMsg, name, i);
}

void Msg::AddField(const char* name, double d)
{
    SkalMsgAddDouble(mMsg, name, d);
}

void Msg::AddField(const char* name, const char* str)
{
    SkalMsgAddString(mMsg, name, str);
}

void Msg::AddField(const char* name, const uint8_t* miniblob, int size_B)
{
    SkalMsgAddMiniblob(mMsg, name, miniblob, size_B);
}

void Msg::AttachBlob(const char* name, std::shared_ptr<Blob> blob)
{
    SkalMsgAttachBlob(mMsg, name, blob->mBlob);
}

bool Msg::HasField(const char* name)
{
    return SkalMsgHasField(mMsg, name);
}

int64_t Msg::GetIntField(const char* name) const
{
    return SkalMsgGetInt(mMsg, name);
}

double Msg::GetDoubleField(const char* name) const
{
    return SkalMsgGetDouble(mMsg, name);
}

const char* Msg::GetStringField(const char* name) const
{
    return SkalMsgGetString(mMsg, name);
}

const uint8_t* Msg::GetMiniblobField(const char* name, int& size_B) const
{
    return SkalMsgGetMiniblob(mMsg, name, &size_B);
}

std::shared_ptr<Blob> Msg::GetBlob(const char* name) const
{
    SkalBlob* blob = SkalMsgGetBlob(mMsg, name);
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

const char* Msg::Name() const
{
    return SkalMsgName(mMsg);
}

const char* Msg::Sender() const
{
    return SkalMsgSender(mMsg);
}

const char* Msg::Recipient() const
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

void CreateThread(const char* name, std::function<bool(Msg&)> processMsg,
            int64_t queueThreshold, int32_t stackSize_B, int64_t xoffTimeout_us)
{
    Thread* t = new Thread(processMsg);

    SkalThreadCfg cfg;
    int n = snprintf(cfg.name, sizeof(cfg.name), "%s", name);
    SKALASSERT(n < (int)sizeof(cfg.name));
    cfg.processMsg = skalCppProcessMsg;
    cfg.cookie = t;
    cfg.queueThreshold = queueThreshold;
    cfg.stackSize_B = stackSize_B;
    cfg.xoffTimeout_us = xoffTimeout_us;
    cfg.statsCount = 0;
    SkalThreadCreate(&cfg);
}

void CreateThread(const char* name, std::function<bool(Msg&)> processMsg)
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
    SKALASSERT(a != NULL);
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
    return Init(NULL);
}

void Exit()
{
    SkalExit();
}

const char* Domain()
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
