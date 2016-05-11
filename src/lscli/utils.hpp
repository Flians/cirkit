/* CirKit: A circuit toolkit
 * Copyright (C) 2009-2015  University of Bremen
 * Copyright (C) 2015-2016  EPFL
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

/**
 * @file utils.hpp
 *
 * @brief Utility functions for the CLI
 *
 * @author Mathias Soeken
 * @since  2.3
 */

#ifndef CLI_UTILS_HPP
#define CLI_UTILS_HPP

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <boost/tokenizer.hpp>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <lscli/command.hpp>
#include <lscli/commands/alias.hpp>
#include <lscli/commands/convert.hpp>
#include <lscli/commands/current.hpp>
#include <lscli/commands/help.hpp>
#include <lscli/commands/print.hpp>
#include <lscli/commands/ps.hpp>
#include <lscli/commands/quit.hpp>
#include <lscli/commands/read_io.hpp>
#include <lscli/commands/show.hpp>
#include <lscli/commands/store.hpp>
#include <lscli/commands/write_io.hpp>

namespace po = boost::program_options;

namespace cirkit
{

namespace detail
{

std::vector<std::string> split_commands( const std::string& commands )
{
  std::vector<std::string> result;
  std::string current;

  enum _state { normal, quote, escape };

  _state s = normal;

  for ( auto c : commands )
  {
    switch ( s )
    {
    case normal:
      switch ( c )
      {
      case '"':
        current += c;
        s = quote;
        break;

      case ';':
        boost::trim( current );
        result.push_back( current );
        current.clear();
        break;

      default:
        current += c;
        break;
      }
      break;

    case quote:
      switch ( c )
      {
      case '"':
        current += c;
        s = normal;
        break;

      case '\\':
        current += c;
        s = escape;
        break;

      default:
        current += c;
        break;
      };
      break;

    case escape:
      current += c;
      s = quote;
      break;
    }
  }

  boost::trim( current );
  if ( !current.empty() )
  {
    result.push_back( current );
  }

  return result;
}

// from http://stackoverflow.com/questions/478898/how-to-execute-a-command-and-get-output-of-command-within-c-using-posix
std::pair<int, std::string> execute_program( const std::string& cmd )
{
  char buffer[128];
  std::string result;
  int exit_status;

  std::shared_ptr<FILE> pipe( popen( cmd.c_str(), "r" ), [&exit_status]( FILE* fp ) { auto status = pclose( fp ); exit_status = WEXITSTATUS( status ); } );
  if ( !pipe )
  {
    throw std::runtime_error( "[e] popen() failed" );
  }
  while ( !feof( pipe.get() ) )
  {
    if ( fgets( buffer, 128, pipe.get() ) != NULL )
    {
      result += buffer;
    }
  }
  return {exit_status, result};
}

bool read_command_line( const std::string& prefix, std::string& line )
{
#ifdef USE_READLINE
  auto * cline = readline( prefix.c_str() );

  /* something went wrong? */
  if ( !cline )
  {
    return false;
  }

  line = cline;
  boost::trim( line );
  free( cline );

  return true;

#else // USE_READLINE

  std::cout << prefix << "> ";
  std::flush(std::cout);
  if( !getline( std::cin, line ) ) {
    return false;
  }

  boost::trim( line );
  return true;
#endif // USE_READLINE
}

}

template<typename S>
int add_store_helper( const environment::ptr& env )
{
  constexpr auto key  = store_info<S>::key;
  constexpr auto name = store_info<S>::name;

  env->add_store<S>( key, name );

  return 0;
}

template<class... S>
class cli_main
{
public:
  cli_main( const std::string& prefix )
    : env( std::make_shared<environment>() ),
      prefix( prefix )
  {
    [](...){}( add_store_helper<S>( env )... );

    /* These are some generic commands that work on all store entries.
     *
     * For most of the commands there are some functions that need to be
     * implemented for the specific store entry types, e.g., for convert
     * one needs to implement store_can_convert and store_convert. Most
     * of the commands have default implementations, some of them throw
     * assertions in the default implementations.
     *
     * see store.hpp for more details
     */
    set_category( "General" );
    insert_command( "alias",   std::make_shared<alias_command>( env ) );
    insert_command( "convert", std::make_shared<convert_command<S...>>( env ) );
    insert_command( "current", std::make_shared<current_command<S...>>( env ) );
    insert_command( "help",    std::make_shared<help_command>( env ) );
    insert_command( "quit",    std::make_shared<quit_command>( env ) );
    insert_command( "show",    std::make_shared<show_command<S...>>( env ) );
    insert_command( "store",   std::make_shared<store_command<S...>>( env ) );
    insert_command( "print",   std::make_shared<print_command<S...>>( env ) );
    insert_command( "ps",      std::make_shared<ps_command<S...>>( env ) );

    opts.add_options()
      ( "command,c", po::value( &command ), "process semicolon-separated list of commands" )
      ( "file,f",    po::value( &file ),    "process file with new-line seperated list of commands" )
      ( "echo,e",                           "echos the command if read from command line or file" )
      ( "counter,n",                        "show a counter in the prefix" )
      ( "log,l",     po::value( &logname ), "logs the execution and stores many statistical information" )
      ( "help,h",                           "produce help message" )
      ;
  }

  void set_category( const std::string& _category )
  {
    category = _category;
  }

  void insert_command( const std::string& name, const std::shared_ptr<command>& cmd )
  {
    env->categories[category].push_back( name );
    env->commands[name] = cmd;
  }

  template<typename Tag>
  void insert_read_command( const std::string& name, const std::string& label )
  {
    insert_command( name, std::make_shared<read_io_command<Tag, S...>>( env, label ) );
  }

  template<typename Tag>
  void insert_write_command( const std::string& name, const std::string& label )
  {
    insert_command( name, std::make_shared<write_io_command<Tag, S...>>( env, label ) );
  }

  int run( int argc, char ** argv )
  {
    po::store( po::command_line_parser( argc, argv ).options( opts ).run(), vm );
    po::notify( vm );

    if ( vm.count( "help" ) || ( vm.count( "command" ) && vm.count( "file" ) ) )
    {
      std::cout << opts << std::endl;
      return 1;
    }

    read_aliases();

    if ( vm.count( "log" ) )
    {
      env->log = true;
      env->start_logging( logname );
    }

    if ( vm.count( "command" ) )
    {
      std::vector<std::string> split;
      boost::algorithm::split( split, command, boost::is_any_of( ";" ), boost::algorithm::token_compress_on );

      auto collect_commands = false;
      std::string batch_string;
      std::string abc_opts;
      for ( auto& line : split )
      {
        boost::trim( line );
        if ( collect_commands )
        {
          batch_string += ( line + "; " );
          if ( line == "quit" )
          {
            if ( vm.count( "echo" ) ) { std::cout << get_prefix() << "abc -c \"" + batch_string << "\"" << std::endl; }
            std::cout << "abc" << ' ' << abc_opts << ' ' << batch_string << '\n';
            execute_line( ( boost::format("abc %s-c \"%s\"") % abc_opts % batch_string ).str() );
            batch_string.clear();
            collect_commands = false;
          }
        }
        else
        {
          if ( boost::starts_with( line, "abc" ) )
          {
            collect_commands = true;
            abc_opts = ( line.size() > 4u ? (line.substr( 4u ) + " ") : "" );
          }
          else
          {
            if ( vm.count( "echo" ) ) { std::cout << get_prefix() << line << std::endl; }
            if ( !execute_line( preprocess_alias( line ) ) )
            {
              return 1;
            }
          }
        }

        if ( env->quit ) { break; }
      }
    }
    else if ( vm.count( "file" ) )
    {
      process_file( file, vm.count( "echo" ) );
    }
    else
    {
#ifdef USE_READLINE
      initialize_readline();
#endif

      std::string line;
      while ( !env->quit && detail::read_command_line( get_prefix(), line ) )
      {
        execute_line( preprocess_alias( line ) );
#ifdef USE_READLINE
        add_history( line.c_str() );
#endif
      }
    }

    if ( env->log )
    {
      env->stop_logging();
    }

    return 0;
  }

public:
  std::shared_ptr<environment> env;

private:
  bool execute_line( const std::string& line )
  {
    /* split commands if line contains a semi-colon */
    if ( !line.empty() && line[0] != '!' && line.find( ';' ) != std::string::npos )
    {
      auto result = true;
      boost::tokenizer<boost::escaped_list_separator<char>> tok( line, boost::escaped_list_separator<char>( '\\', ';', '\"' ) );

      const auto lines = detail::split_commands( line );

      if ( lines.size() > 1u )
      {
        for ( const auto& cline : lines )
        {
          result = result && execute_line( preprocess_alias( cline ) );
        }

        return result;
      }
    }

    /* ignore comments and empty lines */
    if ( line.empty() || line[0] == '#' ) { return false; }

    /* escape to shell */
    if ( line[0] == '!' )
    {
      const auto now = std::chrono::system_clock::now();
      const auto result = detail::execute_program( line.substr( 1u ) );

      if ( env->log )
      {
        command::log_map_t log;
        log["status"] = result.first;
        log["output"] = result.second;
        env->log_command( command::log_opt_t( log ), line, now );
      }

      return true;
    }

    std::vector<std::string> vline;
    boost::tokenizer<boost::escaped_list_separator<char>> tok( line, boost::escaped_list_separator<char>( '\\', ' ', '\"' ) );

    for ( const auto& s : tok )
    {
      if ( !s.empty() )
      {
        vline.push_back( s );
      }
    }

    const auto it = env->commands.find( vline.front() );
    if ( it != env->commands.end() )
    {
      const auto now = std::chrono::system_clock::now();
      const auto result = it->second->run( vline );

      if ( result && env->log )
      {
        env->log_command( it->second, line, now );
      }

      return result;
    }
    else
    {
      std::cout << "unknown command: " << vline.front() << std::endl;
      return false;
    }
  }

  /**
   * @param filename filename with commands
   * @param echo     true, if command should be echoed before execution
   *
   * @return true, if program should exit after this call
   */
  bool process_file( const std::string& filename, bool echo )
  {
    std::ifstream in( filename.c_str(), std::ifstream::in );
    std::string line;

    while ( getline( in, line ) )
    {
      boost::trim( line );

      if ( echo )
      {
        std::cout << get_prefix() << line << std::endl;
      }

      execute_line( preprocess_alias( line ) );

      if ( env->quit )
      {
        /* quit */
        return true;
      }
    }

    /* do not quit */
    return false;
  }

  std::string get_prefix()
  {
    if ( vm.count( "counter" ) )
    {
      return prefix + boost::str( boost::format( " %d> " ) % counter++ );
    }
    else
    {
      return prefix + "> ";
    }
  }

  void read_aliases()
  {
    if ( auto* path = std::getenv( "CIRKIT_HOME" ) )
    {
      std::string alias_path = boost::str( boost::format( "%s/alias" ) % path );
      if ( !boost::filesystem::exists( alias_path ) ) { return; }

      process_file( alias_path, false );
    }
  }

  std::string preprocess_alias( const std::string& line )
  {
    boost::smatch m;

    for ( const auto& p : env->aliases )
    {
      if ( boost::regex_match( line, m, boost::regex( p.first ) ) )
      {
        auto fmt = boost::format( p.second );

        for ( auto i = 1u; i < m.size(); ++i )
        {
          fmt = fmt % std::string( m[i] );
        }
        return fmt.str();
      }
    }

    return line;
  }

#ifdef USE_READLINE
private:
  static cli_main<S...>*   instance;
  std::vector<std::string> command_names;

  void initialize_readline()
  {
    instance = this;

    for ( const auto& p : env->commands )
    {
      command_names.push_back( p.first );
    }
    rl_attempted_completion_function = cli_main<S...>::readline_completion_s;
  }

  static char ** readline_completion_s( const char* text, int start, int end )
  {
    if ( start == 0 )
    {
      return rl_completion_matches( text, []( const char* text, int state ) { return instance->command_iterator( text, state ); } );
    }
    else
    {
      return nullptr;
    }
  }

  char * command_iterator( const char * text, int state )
  {
    static std::vector<std::string>::const_iterator it;

    if ( state == 0 )
    {
      it = command_names.begin();
    }

    while ( it != command_names.end() )
    {
      const auto& name = *it++;
      if ( name.find( text ) != std::string::npos )
      {
        char * completion = new char[name.size()];
        strcpy( completion, name.c_str() );
        return completion;
      }
    }

    return nullptr;
  }
#endif

private:
  std::string             prefix;

  po::options_description opts;
  po::variables_map       vm;

  std::string             category;
  std::string             command;
  std::string             file;
  std::string             logname;

  unsigned                counter = 1u;
};

#define CIRKIT_S(x) #x
#define CIRKIT_SX(x) CIRKIT_S(x)

#define ADD_COMMAND( name ) cli.insert_command( #name, std::make_shared<name##_command>( cli.env ) );
#define ADD_READ_COMMAND( name, label ) cli.insert_read_command<io_##name##_tag_t>( "read_" CIRKIT_SX(name), label );
#define ADD_WRITE_COMMAND( name, label ) cli.insert_write_command<io_##name##_tag_t>( "write_" CIRKIT_SX(name), label );

#ifdef USE_READLINE
template<class... S>
cli_main<S...>* cli_main<S...>::instance = nullptr;
#endif

}

#endif

// Local Variables:
// c-basic-offset: 2
// eval: (c-set-offset 'substatement-open 0)
// eval: (c-set-offset 'innamespace 0)
// End:
