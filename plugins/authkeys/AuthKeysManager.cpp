/*
 * AuthKeysManager.cpp - implementation of AuthKeysManager class
 *
 * Copyright (c) 2018 Tobias Junghans <tobydox@users.sf.net>
 *
 * This file is part of Veyon - http://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <QDir>
#include <QFileInfo>

#include "AuthKeysManager.h"
#include "CommandLineIO.h"
#include "CryptoCore.h"
#include "Filesystem.h"
#include "VeyonConfiguration.h"


AuthKeysManager::AuthKeysManager( QObject* parent ) :
	QObject( parent ),
	m_keyTypePrivate( QStringLiteral("private") ),
	m_keyTypePublic( QStringLiteral("public") ),
	m_checkPermissions( tr( "Please check your permissions." ) ),
	m_invalidKeyName( tr( "Key name contains invalid characters!" ) ),
	m_invalidKeyType( tr( "Invalid key type specified! Please specify \"%1\" or \"%2\"." ).arg( m_keyTypePrivate, m_keyTypePublic ) ),
	m_keyDoesNotExist( tr( "Specified key does not exist! Please use the \"list\" command to list all installed keys." ) ),
	m_keysAlreadyExists( tr( "One or more key files already exist! Please delete them using the \"delete\" command." ) ),
	m_resultMessage()
{
}



AuthKeysManager::~AuthKeysManager()
{
}



bool AuthKeysManager::isKeyNameValid( const QString& name )
{
	return QRegExp( "\\w+").exactMatch( name );
}



bool AuthKeysManager::createKeyPair( const QString& name )
{
	if( isKeyNameValid( name ) == false)
	{
		m_resultMessage = m_invalidKeyName;
		return false;
	}

	const auto privateKeyFileName = VeyonCore::filesystem().privateKeyPath( name );
	const auto publicKeyFileName = VeyonCore::filesystem().publicKeyPath( name );

	if( QFileInfo( privateKeyFileName ).exists() || QFileInfo( publicKeyFileName ).exists() )
	{
		m_resultMessage = m_keysAlreadyExists;
		return false;
	}

	CommandLineIO::print( tr( "Creating new key pair for \"%1\"" ).arg( name ) );

	const auto privateKey = CryptoCore::KeyGenerator().createRSA( CryptoCore::RsaKeySize );
	const auto publicKey = privateKey.toPublicKey();

	if( privateKey.isNull() || publicKey.isNull() )
	{
		m_resultMessage = tr( "Failed to create public or private key!" );
		return false;
	}

	if( writePrivateKeyFile( privateKey, privateKeyFileName ) == false ||
			writePublicKeyFile( publicKey, publicKeyFileName ) == false )
	{
		// m_resultMessage already set by write functions
		return false;
	}

	m_resultMessage = tr( "Newly created key pair has been saved to \"%1\" and \"%2\"." ).arg( privateKeyFileName, publicKeyFileName );

	return true;
}



bool AuthKeysManager::deleteKey( const QString& name, const QString& type )
{
	if( isKeyNameValid( name ) == false)
	{
		m_resultMessage = m_invalidKeyName;
		return false;
	}

	const auto keyFileName = keyFilePathFromType( name, type );

	if( keyFileName.isEmpty() )
	{
		m_resultMessage = m_invalidKeyType;
		return false;
	}

	if( QFileInfo( keyFileName ).exists() == false )
	{
		m_resultMessage = m_keyDoesNotExist;
		return false;
	}

	QFile keyFile( keyFileName );
	keyFile.setPermissions( QFile::WriteOwner | QFile::WriteGroup | QFile::WriteOther );

	if( keyFile.remove() == false )
	{
		m_resultMessage = tr( "Could not remove key file \"%1\"!" ).arg( keyFileName ) + " " + m_checkPermissions;
		return false;
	}

	auto keyFileDirectory = QFileInfo( keyFileName ).absoluteDir();
	auto keyFileBaseDirectory = keyFileDirectory;
	keyFileBaseDirectory.cdUp();

	if( keyFileBaseDirectory.rmdir( keyFileDirectory.dirName() ) == false )
	{
		m_resultMessage = tr( "Could not remove key file directory \"%1\"!" ).arg( keyFileDirectory.path() ) + " " + m_checkPermissions;
		return false;
	}

	return true;
}



bool AuthKeysManager::exportKey( const QString& name, const QString& type, const QString& outputFile )
{
	if( isKeyNameValid( name ) == false)
	{
		m_resultMessage = m_invalidKeyName;
		return false;
	}

	const auto keyFileName = keyFilePathFromType( name, type );

	if( keyFileName.isEmpty() )
	{
		m_resultMessage = m_invalidKeyType;
		return false;
	}

	QFileInfo keyFileInfo( keyFileName );

	if( keyFileInfo.exists() == false )
	{
		m_resultMessage = m_keyDoesNotExist;
		return false;
	}

	if( keyFileInfo.isReadable() == false )
	{
		m_resultMessage = tr( "Failed to read key file." ) + " " + m_checkPermissions;
		return false;
	}

	if( VeyonCore::filesystem().ensurePathExists( QFileInfo( outputFile ).path() ) == false )
	{
		m_resultMessage = tr( "Failed to create directory for output file." );
		return false;
	}

	if( QFileInfo( outputFile ).exists() )
	{
		m_resultMessage = tr( "File \"%1\" already exists." ).arg( outputFile );
		return false;
	}

	if( QFile::copy( keyFileName, outputFile ) == false )
	{
		m_resultMessage = tr( "Failed to write output file." ) + " " + m_checkPermissions;
		return false;
	}

	m_resultMessage = tr( "Key \"%1/%2\" has been exported to \"%3\" successfully." ).arg( name, type, outputFile );

	return true;
}



bool AuthKeysManager::importKey( const QString& name, const QString& type, const QString& inputFile )
{
	if( isKeyNameValid( name ) == false)
	{
		m_resultMessage = m_invalidKeyName;
		return false;
	}

	if( QFileInfo( inputFile ).isReadable() == false )
	{
		m_resultMessage = tr( "Failed read input file." ) + " " + m_checkPermissions;
		return false;
	}

	QString keyFileName;

	if( type == m_keyTypePrivate )
	{
		const auto privateKey = CryptoCore::PrivateKey( inputFile );
		if( privateKey.isNull() || privateKey.isPrivate() == false )
		{
			m_resultMessage = tr( "File \"%1\" does not contain a valid private key!" ).arg( inputFile );
			return false;
		}

		keyFileName = VeyonCore::filesystem().privateKeyPath( name );
	}
	else if( type == m_keyTypePublic )
	{
		const auto publicKey = CryptoCore::PublicKey( inputFile );
		if( publicKey.isNull() || publicKey.isPublic() == false )
		{
			m_resultMessage = tr( "File \"%1\" does not contain a valid public key!" ).arg( inputFile );
			return false;
		}

		keyFileName = VeyonCore::filesystem().publicKeyPath( name );
	}
	else
	{
		m_resultMessage = m_invalidKeyType;
		return false;
	}

	if( QFileInfo( keyFileName ).exists() )
	{
		m_resultMessage = m_keysAlreadyExists;
		return false;
	}

	if( VeyonCore::filesystem().ensurePathExists( QFileInfo( keyFileName ).path() ) == false )
	{
		m_resultMessage = tr( "Failed to create directory for key file." ) + " " + m_checkPermissions;
		return false;
	}

	if( QFile::copy( inputFile, keyFileName ) == false )
	{
		m_resultMessage = tr( "Failed to write key file \"%1\"." ).arg( keyFileName ) + " " + m_checkPermissions;
		return false;
	}

	if( setKeyFilePermissions( name, type ) == false )
	{
		m_resultMessage = tr( "Failed to set permissions for key file \"%1\"!" ).arg( keyFileName ) + " " + m_checkPermissions;
		return false;
	}

	m_resultMessage = tr( "Key \"%1/%2\" has been imported successfully. Please check file permissions of \"%3\" "
						  "in order to prevent unauthorized accesses." ).arg( name, type, keyFileName );

	return true;
}



QStringList AuthKeysManager::listKeys()
{
	const auto privateKeyBaseDir = VeyonCore::filesystem().expandPath( VeyonCore::config().privateKeyBaseDir() );
	const auto privateKeyDirs = QDir( privateKeyBaseDir ).entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );

	const auto publicKeyBaseDir = VeyonCore::filesystem().expandPath( VeyonCore::config().publicKeyBaseDir() );
	const auto publicKeyDirs = QDir( publicKeyBaseDir ).entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );

	QStringList keys;
	keys.reserve( privateKeyDirs.size() + publicKeyDirs.size() );

	for( const auto& privateKeyDir : privateKeyDirs )
	{
		if( QFileInfo( keyFilePathFromType( privateKeyDir, m_keyTypePrivate) ).isFile() )
		{
			keys.append( QStringLiteral("%1/%2").arg( privateKeyDir, m_keyTypePrivate ) );
		}
	}

	for( const auto& publicKeyDir : publicKeyDirs )
	{
		if( QFileInfo( keyFilePathFromType( publicKeyDir, m_keyTypePublic ) ).isFile() )
		{
			keys.append( QStringLiteral("%1/%2").arg( publicKeyDir, m_keyTypePublic ) );
		}
	}

	return keys;
}



bool AuthKeysManager::extractPublicFromPrivateKey( const QString& name )
{
	if( isKeyNameValid( name ) == false)
	{
		m_resultMessage = m_invalidKeyName;
		return false;
	}

	const auto privateKeyFileName = VeyonCore::filesystem().privateKeyPath( name );
	const auto publicKeyFileName = VeyonCore::filesystem().publicKeyPath( name );

	if( QFileInfo( privateKeyFileName ).exists() == false )
	{
		m_resultMessage = m_keyDoesNotExist;
		return false;
	}

	if( QFileInfo( publicKeyFileName ).exists() )
	{
		m_resultMessage = m_keysAlreadyExists;
		return false;
	}

	const auto publicKey = CryptoCore::PrivateKey( privateKeyFileName ).toPublicKey();
	if( publicKey.isNull() || publicKey.isPublic() == false )
	{
		m_resultMessage = tr( "Failed to convert private key to public key" );
		return false;
	}

	return writePublicKeyFile( publicKey, publicKeyFileName );
}



bool AuthKeysManager::writePrivateKeyFile( const CryptoCore::PrivateKey& privateKey, const QString& privateKeyFileName )
{
	if( VeyonCore::filesystem().ensurePathExists( QFileInfo( privateKeyFileName ).path() ) == false )
	{
		m_resultMessage = tr( "Failed to create directory for private key file \"%1\"." ).arg( privateKeyFileName) + " " + m_checkPermissions;
		return false;
	}

	if( privateKey.toPEMFile( privateKeyFileName ) == false )
	{
		m_resultMessage = tr( "Failed to save private key in file \"%1\"!" ).arg( privateKeyFileName ) + " " + m_checkPermissions;
		return false;
	}

	if( setPrivateKeyFilePermissions( privateKeyFileName ) == false )
	{
		m_resultMessage = tr( "Failed to set permissions for private key file \"%1\"!" ).arg( privateKeyFileName ) + " " + m_checkPermissions;
		return false;
	}

	return true;
}



bool AuthKeysManager::writePublicKeyFile( const CryptoCore::PublicKey& publicKey, const QString& publicKeyFileName )
{
	if(	VeyonCore::filesystem().ensurePathExists( QFileInfo( publicKeyFileName ).path() ) == false )
	{
		m_resultMessage = tr( "Failed to create directory for public key file \"%1\"." ).arg( publicKeyFileName ) + " " + m_checkPermissions;
		return false;
	}

	if( publicKey.toPEMFile( publicKeyFileName ) == false )
	{
		m_resultMessage = tr( "Failed to save public key in file \"%1\"!" ).arg( publicKeyFileName ) + " " + m_checkPermissions;
		return false;
	}

	if( setPublicKeyFilePermissions( publicKeyFileName ) == false )
	{
		m_resultMessage = tr( "Failed to set permissions for public key file \"%1\"!" ).arg( publicKeyFileName ) + " " + m_checkPermissions;
		return false;
	}

	return true;
}



QString AuthKeysManager::keyFilePathFromType( const QString& name, const QString& type ) const
{
	if( type == m_keyTypePrivate )
	{
		return VeyonCore::filesystem().privateKeyPath( name );
	}
	else if( type == m_keyTypePublic )
	{
		return VeyonCore::filesystem().publicKeyPath( name );
	}

	return QString();
}



bool AuthKeysManager::setKeyFilePermissions( const QString& name, const QString& type ) const
{
	const auto keyFilePath = keyFilePathFromType( name, type );

	if( type == m_keyTypePrivate )
	{
		return setPrivateKeyFilePermissions( keyFilePath );
	}
	else if( type == m_keyTypePublic )
	{
		return setPublicKeyFilePermissions( keyFilePath );
	}

	return false;
}



bool AuthKeysManager::setPrivateKeyFilePermissions( const QString& fileName ) const
{
	return QFile::setPermissions( fileName, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup );
}



bool AuthKeysManager::setPublicKeyFilePermissions( const QString& fileName ) const
{
	return QFile::setPermissions( fileName, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther );
}
