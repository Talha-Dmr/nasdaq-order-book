#ifndef OBJECT_POOL_HPP
#define OBJECT_POOL_HPP

#include <cstddef>
#include <stdexcept>
#include <vector>

template <typename T> class ObjectPool {
public:
  // Constructor: Başlangıçta belirlenen sayıda nesne için yer ayırır
  ObjectPool(size_t initial_size) {
    pool_.resize(initial_size);
    free_list_.reserve(initial_size);
    // Boş listesini, havuzdaki tüm nesnelerin adresleriyle doldur
    for (auto &item : pool_) {
      free_list_.push_back(&item);
    }
  }

  // Havuzdan bir nesne al
  T *acquire() {
    if (free_list_.empty()) {
      // Gerçek bir sistemde havuzu büyütmek veya daha spesifik bir hata
      // yönetimi gerekir.
      throw std::runtime_error("ObjectPool is empty!");
    }
    T *object = free_list_.back();
    free_list_.pop_back();
    return object;
  }

  // Bir nesneyi havuza geri ver
  void release(T *object) {
    // Nesnenin geçerli bir adreste olduğundan emin olmak için ek kontroller
    // yapılabilir.
    free_list_.push_back(object);
  }

private:
  std::vector<T> pool_;        // Nesnelerin kendisini tutan ana bellek alanı
  std::vector<T *> free_list_; // Boştaki nesnelerin işaretçilerini tutan liste
};

#endif // OBJECT_POOL_HPP
