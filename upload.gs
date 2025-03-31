function doPost(e) {
    let data = Utilities.base64Decode(e.parameters.data);
    let nombreArchivo = Utilities.formatDate(new Date(), 'JST', 'yyyyMMddHHmmss') + ".jpg";
    //let blob = Utilities.newBlob(data, e.parameters.mimetype, nombreArchivo);
    let blob = Utilities.newBlob(data, "image/jpeg", nombreArchivo);
    
    // Save the photo to Google Drive
    let folder, folders = DriveApp.getFoldersByName("M5Camera");
    if (folders.hasNext()) {
        folder = folders.next();
    } else {
        folder = DriveApp.createFolder("M5Camera");
    }
    let file = folder.createFile(blob);
    return ContentService.createTextOutput('Complete')
}
