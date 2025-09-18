#ifndef ITCH_MESSAGES_HPP
#define ITCH_MESSAGES_HPP

#include <cstdint>

// ITCH 5.0 message layouts (packed)
#pragma pack(push, 1)

struct CommonHeader {
  char messageType;           // 'S','R','A','F','E','C','X','D','U'
  uint16_t stockLocate;
  uint16_t trackingNumber;    // network-endian in packet
};

struct SystemEventMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  char eventCode;
};

struct StockDirectoryMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  char stockSymbol[8];
  char marketCategory;
  char financialStatusIndicator;
  uint32_t roundLotSize;
  char roundLotsOnly;
  char issueClassification;
  char issueSubType[2];
  char authenticity;
  char shortSaleThresholdIndicator;
  char ipoFlag;
  char luldReferencePriceTier;
  char etpFlag;
  uint32_t etpLeverageFactor;
  char inverseIndicator;
};

struct AddOrderMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t orderReferenceNumber;
  char buySellIndicator; // 'B' or 'S'
  uint32_t shares;
  char stockSymbol[8];
  uint32_t price;        // 1/10000 dollars
};

struct AddOrderWithMPIDMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t orderReferenceNumber;
  char buySellIndicator;
  uint32_t shares;
  char stockSymbol[8];
  uint32_t price;
  char attribution[4];
};

struct OrderExecutedMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t orderReferenceNumber;
  uint32_t executedShares;
  uint64_t matchNumber;
};

struct OrderExecutedWithPriceMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t orderReferenceNumber;
  uint32_t executedShares;
  uint64_t matchNumber;
  char printable;
  uint32_t executionPrice;
};

struct OrderCancelMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t orderReferenceNumber;
  uint32_t canceledShares;
};

struct OrderDeleteMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t orderReferenceNumber;
};

struct OrderReplaceMessage {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timestamp[6];
  uint64_t originalOrderReferenceNumber;
  uint64_t newOrderReferenceNumber;
  uint32_t shares;
  uint32_t price;
};

#pragma pack(pop)

// Helper: return size of message for type (0 if unknown)
static inline uint32_t itch_message_size(char type) {
  switch (type) {
    case 'S': return sizeof(SystemEventMessage);
    case 'R': return sizeof(StockDirectoryMessage);
    case 'A': return sizeof(AddOrderMessage);
    case 'F': return sizeof(AddOrderWithMPIDMessage);
    case 'E': return sizeof(OrderExecutedMessage);
    case 'C': return sizeof(OrderExecutedWithPriceMessage);
    case 'X': return sizeof(OrderCancelMessage);
    case 'D': return sizeof(OrderDeleteMessage);
    case 'U': return sizeof(OrderReplaceMessage);
    default:  return 0u;
  }
}

#endif // ITCH_MESSAGES_HPP

