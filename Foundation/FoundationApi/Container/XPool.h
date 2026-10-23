#ifndef __Api_Container_XPool_h__
#define __Api_Container_XPool_h__

#include "MultiSys.hpp"

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <new>
#include <utility>

namespace Foundation {
    template <typename T, int ChunkCount = 1, int ChunkSize = 256>
    class XPool {
        enum {
            InUse,
            IsFree
        };

        struct ChunkList;

        struct Chunk {
            alignas(T) UInt8 _Buffer[sizeof(T)];
            ChunkList* _Parent;
            Chunk* _Prev;
            Chunk* _Next;
            int _Len;
            Int8 _State;
#ifdef _DEBUG
            char _File[128];
            int _Line;
#endif

            __forceinline T* GetData() {
                return std::launder(reinterpret_cast<T*>(&_Buffer[0]));
            }
        };

        struct ChunkList {
            int _Count;
            ChunkList* _Prev;
            ChunkList* _Next;
            Chunk _Chunks[ChunkSize];
        };

    public:
        XPool() : _Head(nullptr), _ListHead(nullptr), _ChunkCount(0) {
            static_assert(offsetof(Chunk, _Buffer) == 0, "Chunk storage must stay at offset 0");
            AllocChunk(ChunkCount);
        }

        ~XPool() {
            while (_ListHead != nullptr) {
                ChunkList* chunkList = _ListHead;
                _ListHead = _ListHead->_Next;
                FreeMemory(chunkList);
            }
        }

#ifdef _DEBUG
        T* CreateObject(const char* file, const int line) {
            std::lock_guard<std::mutex> lock(_Lock);
            Chunk* chunk = Create(file, line);
#else
        T* CreateObject() {
            std::lock_guard<std::mutex> lock(_Lock);
            Chunk* chunk = Create();
#endif
            return new (chunk->GetData()) T();
        }

        template<typename... Args>
#ifdef _DEBUG
        T* CreateObject(const char* file, const int line, Args&&... args) {
            std::lock_guard<std::mutex> lock(_Lock);
            Chunk* chunk = Create(file, line);
#else
        T* CreateObject(Args&&... args) {
            std::lock_guard<std::mutex> lock(_Lock);
            Chunk* chunk = Create();
#endif
            return new (chunk->GetData()) T(std::forward<Args>(args)...);
        }

#ifdef _DEBUG
        void RecoverObject(T* value, const char* file, const int line) {
#else
        void RecoverObject(T* value) {
#endif
            FoundationAssert(nullptr != value, "RecoverObject nullptr");
            value->~T();
            std::lock_guard<std::mutex> lock(_Lock);
            Recover(GetChunk(value));
        }

        int Count() const {
            return _ChunkCount * ChunkSize;
        }

    private:
#ifdef _DEBUG
        Chunk* Create(const char* file, const int line) {
#else
        Chunk* Create() {
#endif
            if (_Head == nullptr) {
                AllocChunk(1);
            }

            Chunk* result = _Head;
            Remove(_Head);
            FoundationAssert(result->_State == IsFree && result->_Len == sizeof(Chunk), "chunk invalid");
            ++result->_Parent->_Count;
            result->_State = InUse;
#ifdef _DEBUG
            std::snprintf(result->_File, sizeof(result->_File), "%s", file);
            result->_Line = line;
#endif
            return result;
        }

        void Recover(Chunk* chunk) {
            FoundationAssert(chunk->_State == InUse && chunk->_Len == sizeof(Chunk), "Recover invalid chunk");
            FoundationAssert(chunk->_Parent->_Count > 0, "chunk list error");
            --chunk->_Parent->_Count;
            chunk->_State = IsFree;

            if (chunk->_Parent->_Count == 0 && _ChunkCount > ChunkCount) {
                FreeChunkList(chunk->_Parent);
            }
            else {
                Add(chunk);
            }
        }

        void AllocChunk(const int count) {
            for (int i = 0; i < count; ++i) {
                ChunkList* chunkList = AllocMemory();
                FoundationAssert(chunkList != nullptr, "xnew chunk failed");
                ArrangeChunkList(chunkList);

                chunkList->_Next = _ListHead;
                if (_ListHead != nullptr) {
                    _ListHead->_Prev = chunkList;
                }
                _ListHead = chunkList;
            }

            _ChunkCount += count;
        }

        void FreeChunkList(ChunkList* chunkList) {
            for (int i = 0; i < ChunkSize; ++i) {
                FoundationAssert(chunkList->_Chunks[i]._State == IsFree, "free chunk list but child chunk is not free");
                Remove(&(chunkList->_Chunks[i]));
            }

            if (chunkList->_Next != nullptr) {
                chunkList->_Next->_Prev = chunkList->_Prev;
            }

            if (chunkList->_Prev != nullptr) {
                chunkList->_Prev->_Next = chunkList->_Next;
            }

            if (_ListHead == chunkList) {
                _ListHead = chunkList->_Next;
            }

            FreeMemory(chunkList);
            --_ChunkCount;
        }

        void ArrangeChunkList(ChunkList* chunkList) {
            chunkList->_Prev = nullptr;
            chunkList->_Next = nullptr;
            chunkList->_Count = 0;
            for (int i = 0; i < ChunkSize; ++i) {
                chunkList->_Chunks[i]._Parent = chunkList;
                chunkList->_Chunks[i]._State = IsFree;
                chunkList->_Chunks[i]._Len = sizeof(Chunk);
                chunkList->_Chunks[i]._Prev = nullptr;
                chunkList->_Chunks[i]._Next = nullptr;
                Add(&(chunkList->_Chunks[i]));
            }
        }

        void Add(Chunk* chunk) {
            chunk->_Next = _Head;
            if (_Head != nullptr) {
                _Head->_Prev = chunk;
            }
            _Head = chunk;
        }

        void Remove(Chunk* chunk) {
            if (chunk->_Next != nullptr) {
                chunk->_Next->_Prev = chunk->_Prev;
            }

            if (chunk->_Prev != nullptr) {
                chunk->_Prev->_Next = chunk->_Next;
            }

            if (_Head == chunk) {
                _Head = chunk->_Next;
            }

            chunk->_Next = nullptr;
            chunk->_Prev = nullptr;
        }

        __forceinline static Chunk* GetChunk(T* value) {
            return reinterpret_cast<Chunk*>(reinterpret_cast<UInt8*>(value) - offsetof(Chunk, _Buffer));
        }

        __forceinline static ChunkList* AllocMemory() {
            return reinterpret_cast<ChunkList*>(::operator new(sizeof(ChunkList), std::align_val_t(alignof(ChunkList))));
        }

        __forceinline static void FreeMemory(ChunkList* chunkList) {
            ::operator delete(chunkList, std::align_val_t(alignof(ChunkList)));
        }

    private:
        Chunk* _Head;
        ChunkList* _ListHead;
        int _ChunkCount;
        std::mutex _Lock;
    };
}

#ifdef _DEBUG
#define XPOOL_CREATE(pool, ...) pool.CreateObject(__FILE__, __LINE__, ##__VA_ARGS__)
#define XPOOL_RELEASE(pool, value) pool.RecoverObject((value), __FILE__, __LINE__)
#else
#define XPOOL_CREATE(pool, ...) pool.CreateObject(__VA_ARGS__)
#define XPOOL_RELEASE(pool, value) pool.RecoverObject(value)
#endif

#endif //__Api_Container_XPool_h__
