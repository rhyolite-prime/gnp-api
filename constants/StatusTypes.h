//
// Created by Emmanuel Addo-Odame on 28/06/2026.
//

#ifndef GNPAPI_STATUSTYPES_H
#define GNPAPI_STATUSTYPES_H
namespace gnp::constants {

    enum StatusTypes {
        DRAFT = 0,
        PUBLISHED = 1,
        ARCHIVED = 2,
        DELETED = 3,
        ENABLED = 4,
        DISABLED = 5,
        UPCOMING = 6,
        ONGOING = 7,
        COMPLETED = 8,
        PROCESSING = 9,
        SCHEDULED = 10,
        ACTIVE = 11,
        INACTIVE = 12,
        SUSPENDED = 13,
        PENDING = 14,
        CONVERTED = 15,
        APPROVED = 16,
        REJECTED = 17,
        PROCESSING_PAYOUT = 18,
        PAID = 19,
        SUBMITTED = 20
    };


    enum FileTypes {
        PDF = 0,
        IMAGE = 1,
        VIDEO = 2,
        AUDIO = 3,
    };



}
#endif //GNPAPI_STATUSTYPES_H