// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GOOGLE_APIS_DRIVE_API_URL_GENERATOR_H_
#define CHROME_BROWSER_GOOGLE_APIS_DRIVE_API_URL_GENERATOR_H_

#include <string>

#include "url/gurl.h"

namespace google_apis {

// This class is used to generate URLs for communicating with drive api
// servers for production, and a local server for testing.
class DriveApiUrlGenerator {
 public:
  // |base_url| is the path to the target drive api server.
  // Note that this is an injecting point for a testing server.
  DriveApiUrlGenerator(const GURL& base_url, const GURL& base_download_url);
  ~DriveApiUrlGenerator();

  // The base URL for communicating with the production drive api server.
  static const char kBaseUrlForProduction[];

  // The base URL for the file download server for production.
  static const char kBaseDownloadUrlForProduction[];

  // Returns a URL to invoke "About: get" method.
  GURL GetAboutGetUrl() const;

  // Returns a URL to invoke "Apps: list" method.
  GURL GetAppsListUrl() const;

  // Returns a URL to fetch a file metadata.
  GURL GetFilesGetUrl(const std::string& file_id) const;

  // Returns a URL to create a resource.
  GURL GetFilesInsertUrl() const;

  // Returns a URL to patch file metadata.
  GURL GetFilesPatchUrl(const std::string& file_id,
                        bool set_modified_date,
                        bool update_viewed_date) const;

  // Returns a URL to copy a resource specified by |file_id|.
  GURL GetFilesCopyUrl(const std::string& file_id) const;

  // Returns a URL to fetch file list.
  GURL GetFilesListUrl(int max_results,
                       const std::string& page_token,
                       const std::string& q) const;

  // Returns a URL to touch a resource specified by |resource_id|.
  GURL GetFileTouchUrl(const std::string& resource_id) const;

  // Returns a URL to trash a resource with the given |resource_id|.
  // Note that the |resource_id| is corresponding to the "file id" in the
  // document: https://developers.google.com/drive/v2/reference/files/trash
  // but we use the term "resource" for consistency in our code.
  GURL GetFileTrashUrl(const std::string& resource_id) const;

  // Returns a URL to fetch a list of changes.
  GURL GetChangesListUrl(bool include_deleted,
                         int max_results,
                         const std::string& page_token,
                         int64 start_change_id) const;

  // Returns a URL to add a resource to a directory with |resource_id|.
  // Note that the |resource_id| is corresponding to the "folder id" in the
  // document: https://developers.google.com/drive/v2/reference/children/insert
  // but we use the term "resource" for consistency in our code.
  GURL GetChildrenUrl(const std::string& resource_id) const;

  // Returns a URL to remove a resource with |child_id| from a directory
  // with |folder_id|.
  // Note that we use the name "folder" for the parameter, in order to be
  // consistent with the drive API document:
  // https://developers.google.com/drive/v2/reference/children/delete
  GURL GetChildrenUrlForRemoval(const std::string& folder_id,
                                const std::string& child_id) const;

  // Returns a URL to initiate uploading a new file.
  GURL GetInitiateUploadNewFileUrl() const;

  // Returns a URL to initiate uploading an existing file specified by
  // |resource_id|.
  GURL GetInitiateUploadExistingFileUrl(const std::string& resource_id) const;

  // Generates a URL for downloading a file.
  GURL GenerateDownloadFileUrl(const std::string& resource_id) const;

 private:
  const GURL base_url_;
  const GURL base_download_url_;

  // This class is copyable hence no DISALLOW_COPY_AND_ASSIGN here.
};

}  // namespace google_apis

#endif  // CHROME_BROWSER_GOOGLE_APIS_DRIVE_API_URL_GENERATOR_H_
