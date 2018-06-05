#include <memory>

#include <alice/alice.hpp>
#include <lorina/aiger.hpp>
#include <mockturtle/io/aiger_reader.hpp>
#include <mockturtle/io/write_bench.hpp>
#include <mockturtle/networks/mig.hpp>
#include <mockturtle/views/depth_view.hpp>
#include <mockturtle/views/mapping_view.hpp>

#include <fmt/format.h>

namespace alice
{

using mig_nt = mockturtle::mapping_view<mockturtle::mig_network, true>;
using mig_t = std::shared_ptr<mig_nt>;

ALICE_ADD_STORE( mig_t, "mig", "m", "MIG", "MIGs" );

ALICE_DESCRIBE_STORE( mig_t, mig )
{
  return fmt::format( "i/o = {}/{}   gates = {}", mig->num_pis(), mig->num_pos(), mig->num_gates() );
}

ALICE_PRINT_STORE_STATISTICS( mig_t, os, mig )
{
  mockturtle::depth_view depth_mig{*mig};
  os << fmt::format( "i/o = {}/{}   gates = {}   level = {}", mig->num_pis(), mig->num_pos(), mig->num_gates(), depth_mig.depth() );
  if ( mig->has_mapping() )
  {
    os << fmt::format( "   luts = {}", mig->num_luts() );
  }
  os << "\n";
}

ALICE_READ_FILE( mig_t, aiger, filename, cmd )
{
  mockturtle::mig_network mig;
  lorina::read_aiger( filename, mockturtle::aiger_reader( mig ) );
  return std::make_shared<mig_nt>( mig );
}

ALICE_WRITE_FILE( mig_t, bench, mig, filename, cmd )
{
  mockturtle::write_bench( *mig, filename );
}

} // namespace alice