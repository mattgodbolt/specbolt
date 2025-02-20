#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace specbolt {

class RegisterFile {
public:
  enum class R8 { A, F, B, C, D, E, H, L, A_, F_, B_, C_, D_, E_, H_, L_ };
  enum class R16 { AF, BC, DE, HL, AF_, BC_, DE_, HL_ };

  // General purpose, paired, shadowable registers
  [[nodiscard]] std::uint8_t get(R8 reg) const;
  void set(R8 reg, std::uint8_t value);
  [[nodiscard]] std::uint16_t get(R16 reg) const;
  void set(R16 reg, std::uint16_t value);

  // Special case registers.
  [[nodiscard]] std::uint16_t ix() const;
  void ix(std::uint16_t ix);
  [[nodiscard]] std::uint16_t iy() const;
  void iy(std::uint16_t iy);
  [[nodiscard]] std::uint8_t ixl() const;
  void ixl(std::uint8_t ixl);
  [[nodiscard]] std::uint8_t iyl() const;
  void iyl(std::uint8_t iyl);
  [[nodiscard]] std::uint8_t ixh() const;
  void ixh(std::uint8_t ixh);
  [[nodiscard]] std::uint8_t iyh() const;
  void iyh(std::uint8_t iyh);
  [[nodiscard]] std::uint16_t sp() const;
  void sp(std::uint16_t sp);
  [[nodiscard]] std::uint16_t pc() const;
  void pc(std::uint16_t pc);
  [[nodiscard]] std::uint8_t r() const;
  void r(std::uint8_t r);
  [[nodiscard]] std::uint8_t i() const;
  void i(std::uint8_t i);

  [[nodiscard]] std::uint16_t wz() const;
  void wz(std::uint16_t wz);

  void exx();
  void ex(R16 lhs, R16 rhs);

  void dump(std::ostream &to, std::string_view prefix) const;

private:
  class RegPair {
  public:
    [[nodiscard]] uint8_t high() const;
    void high(uint8_t high);
    [[nodiscard]] uint8_t low() const;
    void low(uint8_t low);
    [[nodiscard]] uint16_t highlow() const;
    void highlow(uint16_t highlow);

  private:
    uint8_t low_{0xff};
    uint8_t high_{0xff};
  };
  [[nodiscard]] RegPair &reg_for(R8 r8);
  [[nodiscard]] const RegPair &reg_for(R8 r8) const;
  [[nodiscard]] RegPair &reg_for(R16 r16);
  [[nodiscard]] const RegPair &reg_for(R16 r16) const;

  std::array<RegPair, 8> regs_{};
  std::uint16_t ix_{0xffff};
  std::uint16_t iy_{0xffff};
  std::uint16_t sp_{0xffff};
  std::uint16_t wz_{0xffff};
  std::uint16_t pc_{};
  std::uint8_t r_{};
  std::uint8_t i_{};
};

} // namespace specbolt
