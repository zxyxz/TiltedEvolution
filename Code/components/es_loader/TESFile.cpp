#include "TESFile.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace ESLoader
{
namespace
{
std::string FourCC(uint32_t aValue)
{
    char text[5] = {
        static_cast<char>(aValue & 0xFF),
        static_cast<char>((aValue >> 8) & 0xFF),
        static_cast<char>((aValue >> 16) & 0xFF),
        static_cast<char>((aValue >> 24) & 0xFF),
        '\0',
    };
    return std::string(text);
}
} // namespace

TESFile::TESFile(Map<String, uint32_t>& aMasterFiles)
    : m_masterFiles(aMasterFiles)
{
}

void TESFile::Setup(uint8_t aStandardId)
{
    m_standardId = aStandardId;
    m_formIdPrefix = m_standardId * 0x1000000;
}

void TESFile::Setup(uint16_t aLiteId)
{
    m_liteId = aLiteId;
    m_formIdPrefix = 0xFE000000 + (m_liteId * 0x1000);
}

bool TESFile::LoadFile(const std::filesystem::path& acPath) noexcept
{
    m_filename = acPath.filename().string();

    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (file.fail())
    {
        spdlog::warn("Failed to open plugin {} ({}). Skipping.", m_filename, acPath.string());
        return false;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    m_buffer.Resize(fileSize);

    file.read(reinterpret_cast<char*>(m_buffer.GetWriteData()), fileSize);
    if (!file)
    {
        spdlog::error("Short read while loading plugin {} ({}).", m_filename, acPath.string());
        return false;
    }

    return true;
}

bool TESFile::IndexRecords(RecordCollection& aRecordCollection) noexcept
{
    if (m_filename.size() == 0)
        return false;

    Buffer::Reader reader(&m_buffer);

    while (true)
    {
        if (!ReadGroupOrRecord(reader, aRecordCollection))
            break;
    }

    return true;
}

bool TESFile::ReadGroupOrRecord(Buffer::Reader& aReader, RecordCollection& aRecordCollection) noexcept
{
    if (aReader.Eof())
        return false;

    const size_t recordOffset = aReader.GetBytePosition();
    const size_t bufferSize = m_buffer.GetSize();
    uint32_t type = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&type), 4);
    uint32_t size = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4);
    aReader.Reverse(8);

    if (type == static_cast<uint32_t>(FormEnum::GRUP))
    {
        size_t endOfGroup = recordOffset + size;
        if (endOfGroup > bufferSize)
        {
            spdlog::warn("ESLoader: {} GRUP size {} exceeds file size {}; clamping", m_filename.c_str(), size, bufferSize);
            endOfGroup = bufferSize;
        }
        aReader.Advance(sizeof(Group));

        while (aReader.GetBytePosition() < endOfGroup)
        {
            const size_t before = aReader.GetBytePosition();
            if (!ReadGroupOrRecord(aReader, aRecordCollection))
            {
                spdlog::warn("ESLoader: {} GRUP parse hit EOF at {} of {}", m_filename.c_str(), before, endOfGroup);
                break;
            }
            const size_t after = aReader.GetBytePosition();
            if (after <= before)
            {
                spdlog::warn("ESLoader: {} GRUP parse made no progress at {}", m_filename.c_str(), before);
                break;
            }
        }
    }
    else // Records
    {
        if (recordOffset + sizeof(Record) + size > bufferSize)
        {
            spdlog::warn("ESLoader: {} record {} size {} overflows file at {}", m_filename.c_str(), FourCC(type), size, recordOffset);
            return false;
        }

        Record* pRecord = reinterpret_cast<Record*>(m_buffer.GetWriteData() + aReader.GetBytePosition());

        switch (pRecord->GetType())
        {
        case FormEnum::TES4:
        {
            TES4* pFileHeader = reinterpret_cast<TES4*>(pRecord);

            TES4 fileHeader;
            fileHeader.CopyRecordData(*pFileHeader);
            fileHeader.ParseChunks(*pFileHeader, m_parentToFormIdPrefix);

            uint8_t parentId = 0;
            for (const Chunks::MAST& master : fileHeader.m_masterFiles)
            {
                m_parentToFormIdPrefix[parentId] = m_masterFiles[master.m_masterName];
                parentId++;
            }

            m_parentToFormIdPrefix[parentId] = m_formIdPrefix;
            m_parentToFormIdPrefix[0xFE] = m_formIdPrefix;

            break;
        }
        // case FormEnum::ACHR:
        case FormEnum::REFR:
        {
            REFR parsedRecord = CopyAndParseRecord<REFR>(pRecord);
            aRecordCollection.m_objectReferences[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::CELL: break;
        case FormEnum::CLMT:
        {
            CLMT parsedRecord = CopyAndParseRecord<CLMT>(pRecord);
            aRecordCollection.m_climates[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::NPC_:
        {
            NPC parsedRecord = CopyAndParseRecord<NPC>(pRecord);
            aRecordCollection.m_npcs[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::CONT:
        {
            CONT parsedRecord = CopyAndParseRecord<CONT>(pRecord);
            aRecordCollection.m_containers[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::GMST:
        {
            GMST parsedRecord = CopyAndParseRecord<GMST>(pRecord);
            aRecordCollection.m_gameSettings[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::WRLD:
        {
            WRLD parsedRecord = CopyAndParseRecord<WRLD>(pRecord);
            aRecordCollection.m_worlds[parsedRecord.GetFormId()] = parsedRecord;
        }
        case FormEnum::NAVM:
        {
            NAVM parsedRecord = CopyAndParseRecord<NAVM>(pRecord);
            aRecordCollection.m_navMeshes[parsedRecord.GetFormId()] = parsedRecord;
        }
        }

        // pRecord->DiscoverChunks();

        if (pRecord->GetType() != FormEnum::TES4)
        {
            Record record;
            record.CopyRecordData(*pRecord);
            record.SetBaseId(GetFormIdPrefix(pRecord->GetFormId(), m_parentToFormIdPrefix));
            aRecordCollection.m_allRecords[pRecord->GetFormId()] = *pRecord;
        }

        aReader.Advance(sizeof(Record) + size);
    }

    return true;
}

template <typename T>
concept ExpectsGRUP = requires(T t) { &T::ParseGRUP; };

template <class T> T TESFile::CopyAndParseRecord(Record* pRecordHeader)
{
    T* pRecord = reinterpret_cast<T*>(pRecordHeader);

    T parsedRecord;
    parsedRecord.CopyRecordData(*pRecord);
    parsedRecord.SetBaseId(TESFile::GetFormIdPrefix(pRecord->GetFormId(), m_parentToFormIdPrefix));
    parsedRecord.ParseChunks(*pRecord, m_parentToFormIdPrefix);

    // If the record expects a subgroup right after, parse it? Or do we not care since we load everything?
    if constexpr (ExpectsGRUP<T>)
    {
        //    ParseGRUP(pRecord, parsedRecord);
    }

    return parsedRecord;
}

template <class T> void TESFile::ParseGRUP(Record* pRecordHeader, T& aRecord)
{
    // aRecord.ParseGRUP();
}

uint32_t TESFile::GetFormIdPrefix(uint32_t aFormId, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    auto baseId = (uint8_t)(aFormId >> 24);
    const auto masterId = aParentToFormIdPrefix.find(baseId);

    if (masterId == std::end(aParentToFormIdPrefix))
    {
        // TODO: this is weird, but for some reason, in Skyrim.esm,
        // the GMST record with EDID "iDaysToRespawnVendor" has a base id of 0x01
        spdlog::warn("Form id prefix not found: {:X}", baseId);
        return 0;
    }

    return masterId->second;
}

} // namespace ESLoader
