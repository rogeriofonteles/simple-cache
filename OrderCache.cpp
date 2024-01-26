// Your implementation of the OrderCache...
#include "OrderCache.h"


void OrderCache::addOrder(Order order) {
  cache_.addOrderOnCache(std::move(order));
}

void OrderCache::cancelOrder(const std::string& orderId) {
  cache_.removeFromCacheByOrderId(orderId);
}

void OrderCache::cancelOrdersForUser(const std::string& user) {
  cache_.removeFromCacheByUserName(user);
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) {
  cache_.removeFromCacheBySecId(securityId, minQty);
}

unsigned int OrderCache::getMatchingSizeForSecurity(const std::string& securityId) {
  return cache_.getMatchingSizeForSecurity(securityId);
}

std::vector<Order> OrderCache::getAllOrders() const {
  return cache_.getOrdersFromCacheAsVec();
}
