//
// Created by Kanari on 2016/10/16.
//

#include <cstring>
#include <cstddef>
#include "pf.h"
#include "rm.h"
#include "rm_internal.h"

/* RM FileHandle */
RM_FileHandle::RM_FileHandle() {
    recordSize = 0;
}

RM_FileHandle::~RM_FileHandle() {}

RC RM_FileHandle::GetRec(const RID &rid, RM_Record &rec) const {
    if (recordSize == 0) return RM_FILE_NOT_OPENED;
    PageNum pageNum;
    SlotNum slotNum;
    PF_PageHandle pageHandle;
    char *data;
    TRY(rid.GetPageNum(pageNum));
    TRY(rid.GetSlotNum(slotNum));
    if (slotNum >= recordsPerPage || slotNum < 0)
        return RM_SLOTNUM_OUT_OF_RANGE;
    TRY(pfHandle.GetThisPage(pageNum, pageHandle));
    TRY(pageHandle.GetData(data));

    rec.rid = rid;
    rec.SetData(data + pageHeaderSize + recordSize * slotNum, (size_t)recordSize);

    if (nullableNum > 0) {
        bool *isnull = new bool[nullableNum];
        for (int i = 0; i < nullableNum; ++i) {
            isnull[i] = getBitMap(((RM_PageHeader *)data)->bitmap,
                                  recordsPerPage + slotNum * nullableNum + i);
        }
        rec.SetIsnull(isnull, nullableNum);
    }

    TRY(pfHandle.UnpinPage(pageNum));
    return 0;
}

RC RM_FileHandle::InsertRec(const char *pData, RID &rid, bool *isnull) {
    if (recordSize == 0) return RM_FILE_NOT_OPENED;
    PageNum pageNum;
    SlotNum slotNum;
    PF_PageHandle pageHandle;
    char *data, *destination;

    if (firstFreePage != kLastFreePage) {
        TRY(pfHandle.GetThisPage(firstFreePage, pageHandle));
        TRY(pageHandle.GetPageNum(pageNum));
        TRY(pageHandle.GetData(data));
        slotNum = ((RM_PageHeader *)data)->firstFreeRecord;
        destination = data + pageHeaderSize + recordSize * slotNum;
        ((RM_PageHeader *)data)->firstFreeRecord = *(short *)destination;
        if (*(short *)destination == kLastFreeRecord) {
            firstFreePage = ((RM_PageHeader *)data)->nextFreePage;
            isHeaderDirty = true;
        }
    } else {
        TRY(pfHandle.GetLastPage(pageHandle));
        TRY(pageHandle.GetPageNum(pageNum));
        TRY(pageHandle.GetData(data));
        CHECK(((RM_PageHeader *)data)->allocatedRecords <= recordsPerPage);
        if (((RM_PageHeader *)data)->allocatedRecords == recordsPerPage) {
            TRY(pfHandle.UnpinPage(pageNum));
            TRY(pfHandle.AllocatePage(pageHandle));
            TRY(pageHandle.GetPageNum(pageNum));
            TRY(pageHandle.GetData(data));
            *(RM_PageHeader *)data = {kLastFreeRecord, 0, kLastFreePage};
            memset(data + offsetof(RM_PageHeader, bitmap), 0,
                   (size_t)(recordsPerPage * (nullableNum + 1)));
        }
        slotNum = ((RM_PageHeader *)data)->allocatedRecords;
        destination = data + pageHeaderSize + recordSize * slotNum;
        // LOG(INFO) << "recordSize = " << recordSize << " slotnum = " << slotNum;
        // LOG(INFO) << "recordsPerPage = " << recordsPerPage << " allocated = " <<
            // ((RM_PageHeader *)data)->allocatedRecords;
        ++((RM_PageHeader *)data)->allocatedRecords;
    }
    memcpy(destination, pData, (size_t)recordSize);
    setBitMap(((RM_PageHeader *)data)->bitmap, slotNum, true);
    for (int i = 0; i < nullableNum; ++i) {
        setBitMap(((RM_PageHeader *)data)->bitmap,
                  recordsPerPage + slotNum * nullableNum + i, isnull[i]);
    }
    rid = RID(pageNum, slotNum);

    TRY(pfHandle.MarkDirty(pageNum));
    TRY(pfHandle.UnpinPage(pageNum));
    return 0;
}

RC RM_FileHandle::DeleteRec(const RID &rid) {
    if (recordSize == 0) return RM_FILE_NOT_OPENED;
    PageNum pageNum;
    SlotNum slotNum;
    PF_PageHandle pageHandle;
    char *data;
    TRY(rid.GetPageNum(pageNum));
    TRY(rid.GetSlotNum(slotNum));
    if (slotNum >= recordsPerPage || slotNum < 0)
        return RM_SLOTNUM_OUT_OF_RANGE;
    TRY(pfHandle.GetThisPage(pageNum, pageHandle));
    TRY(pageHandle.GetData(data));

    if (getBitMap(((RM_PageHeader *)data)->bitmap, slotNum) == 0)
        return RM_RECORD_DELETED;
    setBitMap(((RM_PageHeader *)data)->bitmap, slotNum, false);
    *(short *)(data + pageHeaderSize + recordSize * slotNum) = ((RM_PageHeader *)data)->firstFreeRecord;
    if (((RM_PageHeader *)data)->firstFreeRecord == kLastFreeRecord) {
        ((RM_PageHeader *)data)->nextFreePage = firstFreePage;
        firstFreePage = pageNum;
        isHeaderDirty = true;
    }
    ((RM_PageHeader *)data)->firstFreeRecord = (short)slotNum;

    TRY(pfHandle.MarkDirty(pageNum));
    TRY(pfHandle.UnpinPage(pageNum));
    return 0;
}

RC RM_FileHandle::UpdateRec(const RM_Record &rec) {
    if (recordSize == 0) return RM_FILE_NOT_OPENED;
    PageNum pageNum;
    SlotNum slotNum;
    PF_PageHandle pageHandle;
    char *data;
    TRY(rec.rid.GetPageNum(pageNum));
    TRY(rec.rid.GetSlotNum(slotNum));
    if (slotNum >= recordsPerPage || slotNum < 0)
        return RM_SLOTNUM_OUT_OF_RANGE;
    TRY(pfHandle.GetThisPage(pageNum, pageHandle));
    TRY(pageHandle.GetData(data));

    memcpy(data + pageHeaderSize + recordSize * slotNum, rec.pData, (size_t)recordSize);
    for (int i = 0; i < nullableNum; ++i) {
        setBitMap(((RM_PageHeader *)data)->bitmap,
                  recordsPerPage + slotNum * nullableNum + i, rec.isnull[i]);
    }

    TRY(pfHandle.MarkDirty(pageNum));
    TRY(pfHandle.UnpinPage(pageNum));
    return 0;
}

RC RM_FileHandle::ForcePages(PageNum pageNum) {
    return pfHandle.ForcePages(pageNum);
}
